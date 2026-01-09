README
Computer Systems and Programming – Winter Exam Project
A.Y. 2025–2026

Name: Aleksa Kotroman
Matricola: 2237712

============================================================
1. HOW TO COMPILE THE PROJECT
============================================================

The project includes a Makefile that compiles both the server and the client.

To compile the entire project:
    make

This command produces two executables:
    - server
    - client

To clean all compiled files:
    make clean

The project is intended to be compiled and tested on Ubuntu 24.04.


============================================================
2. HOW TO START THE SERVER -> NEEDS SUDO
============================================================

Syntax:
    sudo ./server <root_directory> [<IP>] [<port>] -> (it needs to start with sudo because we have to create group and users)

Default values:
    IP   : 127.0.0.1
    Port : 8080

Root directory:
    - The root directory name is chosen by the user
    - If it does not exist, the server creates it automatically
    - All user home directories and files are stored inside this root directory

Examples:
    sudo ./server root_directory
    sudo ./server root_directory 127.0.0.1 8080
    sudo ./server root_directory 127.0.0.1 80

Server console:
    - The ONLY command accepted on the server console is:
        exit
    - When "exit" is typed:
        • The server shuts down
        • All connected clients are disconnected
        • All client processes are terminated
        • No further operations are possible


============================================================
3. HOW TO START THE CLIENT
============================================================

Syntax:
    ./client [<IP> <port>]

Default values:
    IP   : 127.0.0.1
    Port : 8080

Examples:
    ./client
    ./client 127.0.0.1 8080


============================================================
4. CLIENT COMMANDS AND HELP
============================================================

The client provides an interactive interface.

Typing:
    help

prints the complete list of supported commands and their syntax.
it's possible to type help in any moment of client window.


============================================================
5. USER MANAGEMENT COMMANDS
============================================================

Create user:
    create_user <username> <permissions>

Example:
    create_user aleksa 700

Notes:
    - The command is executed from the client
    - A system user is created WITHOUT password
    - A virtual home directory is created inside the server root
    - All users belong to the same group(csapgroup)
    - The group(csapgroup) will be automaticly created the moment we start ./server

Login:
    login <username>

Example:
    login alice

Notes:
    - No password is required

Delete user:
    delete_user <username>

Example:
    delete_user alice

Notes:
    - Must be executed while NOT logged in
    - This option is added just for easier testing


============================================================
6. FILE AND DIRECTORY COMMANDS
============================================================

Change directory:
    cd <path>

List directory:
    list [path]

Create file or directory:
    create <path> <permissions>
    create <path> <permissions> -d

Change permissions:
    chmod <path> <permissions>

Move / rename:
    move <source> <destination>

Notes:
    - It also renames files

Delete file or directory:
    delete <path>


============================================================
7. READ AND WRITE COMMANDS
============================================================

Read file:
    read <path>
    read -offset=N <path>

Examples:
    read file.txt
    read -offset=10 file.txt

Write file:
    write <path>
    write -offset=N <path>

Examples:
    write file.txt
    write -offset=5 file.txt

Notes:
    - The client reads data from standard input
    - The data is sent to the server and written to the file
    - Write offset option works only if file already exists


============================================================
8. UPLOAD AND DOWNLOAD
============================================================

Upload file:
    upload <local_path> <server_path>
    upload -b <local_path> <server_path>

Download file:
    download <server_path> <local_path>
    download -b <server_path> <local_path>

Notes:
    - The "-b" option runs the operation in background
    - The client remains interactive
    - When the background operation finishes, a notification message is printed


============================================================
9. EXIT
============================================================

Client exit:
    exit

Notes:
    - The client cannot exit if background operations are still running

Server exit:
    - Type "exit" in the server console to terminate the server and all clients