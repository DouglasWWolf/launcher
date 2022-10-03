
//==========================================================================================================
// launcher - Provides launcher and process manangement for a system of executables
//==========================================================================================================
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include "config_file.h"
#include "udpsock.h"

// We'll use all of the C++ std tools
using namespace std;

// Define a vector of strings
typedef vector<string> strvec_t;

// A dummy value useful for ignoring return values
volatile int bit_bucket;

// The configuration file
CConfigScript cs;

// This is the UDP port where process management ports start
int udp_base_port;

// These are the types of process management message we can send/receive
enum
{
    CMD_PING = 1,
    RSP_PING = 2,
    CMD_DOWN = 3
};

// Message structures for the message we understand
struct cmd_ping_t {uint16_t cmd; uint16_t port;};
struct rsp_ping_t {uint16_t cmd; uint16_t port;};


//==========================================================================================================
// i_to_s() - Converts an integer to a string
//==========================================================================================================
string i_to_s(int value)
{
    char buffer[30];
    sprintf(buffer, "%i", value);
    return buffer;
}
//==========================================================================================================


//==========================================================================================================
// spawn() - Spawns a new process.
//
// Passed:  args = A string vector full of arguments to be passed as the argv[] array
//
// Returns: true if the new process executed, otherwise false
//==========================================================================================================
bool spawn(strvec_t& args)
{
    int fd[2];
    char buffer[1];
    const char* param[20];

    // This is how many arguments an executable can have
    size_t count = (sizeof(param) / sizeof(param[0])) - 1;

    // Determine how many arguments this executable has
    if (args.size() < count) count = args.size();

    // Convert our strvec_t into an array of const char*
    for (int i=0; i<count; ++i) param[i] = args[i].c_str();

    // execv() requires the last argument in the array to be NULL
    param[count] = NULL;

    // Create a pipe
    bit_bucket = pipe(fd);

    // Ensure that the child process closes the file descriptors on a successful exec()
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);

    // Duplicate our process.  If we are the child process...
    if (fork() == 0)
    {
        // Execute the new program
        execv(param[0], (char* const*)param);
        
        // If we get here, the exec call failed.  Tell the parent process
        bit_bucket = write(fd[1], buffer, 1);
        
        // And we're done
        exit(1);
    }

    // Close the "write" end of the pipe.  Now the only process with the 
    // write-end of the pipe open is the child process we just launched
    close(fd[1]);

    // This read will block until the child process either writes something or closes the descriptor    
    bool success = read(fd[0], buffer, 1) == 0;

    // Close the read-end of the pipe
    close(fd[0]);

    // Tell the caller whether or not the program succesfully spawned
    return success;
}
//==========================================================================================================


//==========================================================================================================
// wait_for_executable() - Waits for an executable to respond to a message
//==========================================================================================================
void wait_for_executable(int port)
{
    UDPSock client, server;

    // This is the port number we'll listen for replies on
    uint16_t reply_port = udp_base_port;

    // The command and response message
    cmd_ping_t ping_cmd;
    rsp_ping_t ping_rsp;

    // Create the socket we'll listen for replies on
    server.create_server(reply_port, "", AF_INET);

    // Create the sending socket
    client.create_sender(port, "localhost", AF_INET);

    // Construct the ping command
    ping_cmd = {CMD_PING, reply_port};

    // We'll sit in a loop sending the process managment message until we get a reply
    while (true)
    {
        // Send the ping command
        client.send(&ping_cmd, sizeof ping_cmd);

        // If we don't get a response back for 100 ms, try again
        if (!server.wait_for_data(100)) continue;

        // Receive a message
        server.receive(&ping_rsp, sizeof ping_rsp);
        
        // If the sender of that message is who we hope it is, break out of the loop
        if (ping_rsp.port == port) break;
    }
}
//==========================================================================================================




//==========================================================================================================
// fetch_config() - Reads in the config file
//==========================================================================================================
void fetch_config()
{
    CConfigFile config;

    // Read in the configuration file
    if (!config.read("launcher.conf")) exit(1);

    // Fetch the specs
    try
    {
        config.get("udp_base_port", &udp_base_port);
        config.get("executables", &cs);
    }
    catch (runtime_error& ex)
    {
        printf("%s\n", ex.what());
        exit(1);
    }

}
//==========================================================================================================



//==========================================================================================================
// kill() - Sends a "drop dead" message to a process management port
//==========================================================================================================
void kill(int port)
{
    UDPSock udp;

    // This is the command we're going to send
    uint16_t cmd = CMD_DOWN;

    // Create a UDP socket for sending message
    udp.create_sender(port, "localhost", AF_INET);

    // Send the message
    udp.send(&cmd, sizeof cmd);

    // And we're done
    udp.close();
}
//==========================================================================================================


//==========================================================================================================
// bring_down_system() - Use the process management ports to bring down the system
//==========================================================================================================
struct exe_t {string name; int port;};

void bring_down_system(bool immediate)
{
    exe_t exe;

    // A vector of the executable names and port # of their management ports
    vector<exe_t> v;

    // An reverse iterator to the vector immediately above
    vector<exe_t>::reverse_iterator it;

    // Rewind the script that contains executables
    cs.rewind();

    // Fetch the UDP port number that serves as our base port
    int port = udp_base_port;

    // Build a vector of executable names and their base ports
    while (cs.get_next_line())
    {
        exe.name = cs.get_next_token();
        exe.port = ++port;
        v.push_back(exe);
    }

    // Walk through our executables in the opposite order in which they were launched
    // and kill them one at a time.  The delay is in order to make sure the executable
    // is really down before we move on to the next one
    for (it = v.rbegin(); it != v.rend(); ++it)
    {
        if (!immediate) printf("Killing %s\n", it->name.c_str());
        kill(it->port);
        if (!immediate) usleep(500000);
    }
}
//==========================================================================================================


//==========================================================================================================
// bring_up_system() - Use the process management ports to bring up the system
//==========================================================================================================
void bring_up_system()
{
    strvec_t args;
    int token_count;

    // Fetch the base port for process management
    int port = udp_base_port;

    // Rewind the script that contains executables
    cs.rewind();

    // Fetch one line at a time from that script
    while (cs.get_next_line(&token_count))
    {
        // So far, we have no program arguments
        args.clear();

        // Fetch all of the tokens of this executable's command line
        for (int i=0; i<token_count; ++i) args.push_back(cs.get_next_token());

        // Add a "-mport" switch to the command line
        args.push_back("-mport");

        // Add the port number as a switch parameter
        args.push_back(i_to_s(++port));

        // Tell the world what we're about to do
        printf("Launching %s\n", args[0].c_str());

        // Spawn this executable
        if (!spawn(args))
        {
            printf("%s doesn't exist!\n", args[0].c_str());
            bring_down_system(false);
            exit(1);
        }

        // And wait for the executable to tell us it's up and ready
        wait_for_executable(port);
    }
}
//==========================================================================================================


//==========================================================================================================
// main() - Command line is:
//             "launcher"
//    -- or -- "launcher down"
//==========================================================================================================
int main(int argc, const char** argv)
{  
    strvec_t program;

    // Read in our configuration file
    fetch_config();

    // Make sure all executables are down
    bring_down_system(true);

    // If the user was just trying to bring the system down, we're done
    if (argv[1] && strcmp(argv[1], "down") == 0) exit(0);

    // Wait 1 second between bringing the system down and bringing it back up
    sleep(1);

    // And bring the system up
    bring_up_system();
}
//==========================================================================================================
