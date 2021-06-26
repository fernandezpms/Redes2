#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFSIZE 512
#define CMDSIZE 4
#define PARSIZE 100
#define SRCPORT 20

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"
#define MSG_200 "200 PORT command successful\r\n"
#define MSG_150 "150 Opening BINARY mode data connection for %s\r\n"

//Auxiliar libraries
#include<ctype.h>

void sig_handler(int sig) {
    if (sig == SIGCHLD) {
        int pid = waitpid(-1, NULL, WNOHANG);
    }
}

/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // receive the command in the buffer and check for errors
    recv_s = read(sd,buffer,BUFSIZE);
    if(recv_s < 0){
        warnx("Error reading buffer");
        return false;
    }
    if(recv_s == 0){
        warnx("Empty buffer");
        return false;
    }


    // expunge the terminator characters from the buffer
    buffer[strcspn(buffer, "\r\n")] = 0;

    // complex parsing of the buffer
    // extract command receive in operation if not set \0
    // extract parameters of the operation in param if it needed
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) != CMDSIZE) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') strcpy(operation, token);
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
    return true;
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...){
    char buffer[BUFSIZE];

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);
    // send answer preformated and check errors

    if(write(sd, buffer, strlen(buffer))<0){
        warn("Error");
        return false;
    }else{
        return true;
    }


}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/

void retr(int sd, struct sockaddr_in * addr ,char *file_path) {
    FILE *file;    
    int bread;
    long fsize;
    int srcsd;
    struct sockaddr_in srcaddr;
    char buffer[BUFSIZE];

    // check if file exists if not inform error to client
    file = fopen(file_path, "r");
    if (file == NULL) {
        send_ans(sd, MSG_550, file_path);
        return;
    }
    // send a success message with the file length
    fseek(file, 0L, SEEK_END);
    fsize = ftell(file);
    rewind(file);
    send_ans(sd, MSG_299, file_path, fsize);

    // open connection to client
    srcsd = socket(AF_INET, SOCK_STREAM, 0);
    srcaddr.sin_family = AF_INET;
    srcaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    srcaddr.sin_port = htons(SRCPORT);
    if (srcsd < 0) err(1, "error opening socket");
    
    addr->sin_port = htons(ntohs(addr->sin_port)+1);
    if (bind(srcsd, (struct sockaddr *) &srcaddr, sizeof(srcaddr)) < 0) err(1, "error al enlazar al puerto origen");
    if (connect(srcsd, (struct sockaddr *) addr, sizeof(srcaddr)) < 0) err(1, "error al conectarse al canal de datos");

    // important delay for avoid problems with buffer size
    // sleep(1);

    // send the file
    while(!feof(file)) {
        bread = fread(buffer, 1, BUFSIZE, file);
        if (write(srcsd, buffer, bread) < 0) warn("error sending data");
    }

    // close connection to client
    close(srcsd);
    // close the file
    fclose(file);
    // send a completed transfer message
    send_ans(sd, MSG_226);
}


void stor(int sd, struct sockaddr_in * addr , char *file_data){
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    int srcsd;
    struct sockaddr_in srcaddr;
    char *file_path, *file_size, *aux;
    FILE *file;

    // send the message 200 Port command succesful
    send_ans(sd,MSG_200);

    //obtengo el path y el tamaño del archivo
    aux = strtok(file_data,"_");
    strcpy(file_path,aux);
    aux = strtok(NULL,"_");
    strcpy(file_size,aux);
    f_size = atoi(file_size);

    // send message whit file information
    send_ans(sd, MSG_150, file_path);

    // open the file to write
    file = fopen(file_path, "w");


    // open connection to client
    srcsd = socket(AF_INET, SOCK_STREAM, 0);
    if (srcsd < 0) errx(2, "Cannot create socket");

    if (connect(srcsd, (struct sockaddr *) &addr, sizeof(addr)) < 0) errx(3, "Error on connect to data channel");

    // recive the file

    while(1){
        recv_s = read(srcsd,buffer,BUFSIZE);
        if(recv_s > 0){
            fwrite(desc,1,recv_s,file);
        }
        if(recv_s < r_size){
            break;
        }

    }
    // colse file
    fclose(file);

    // close connection to client
    close(srcsd);

    // send message complete transfer
    send_ans(sd,MSG_226);
    
}

/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, cred[100];
    size_t len = 0;
    bool found = false;

    // make the credential string
    strcpy(cred, user);
    strcat(cred, ":");
    strcat(cred, pass);
    strcat(cred, "\n");
    // check if ftpusers file it's present
    file = fopen(path, "r");
    // search for credential string
    line = (char *) malloc(100*sizeof(char));
    len = 100;
    while(getline(&line, &len, file)>0){
        if(strcmp(cred, line) == 0){
            found = true;
            //printf("Se encontro el usuario\n");
            break;
        }
    }
    // close file and release any pointes if necessary
    fclose(file);
    // return search status
    return found;
}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];

    // wait to receive USER action
    if(!recv_cmd(sd,"USER", user)){
        warnx("FAIL receiving username");
        return false;
    }
    // ask for password
    if(send_ans(sd,MSG_331,user)){
        warnx("Failed askin for password");
        return false;
    }  



    // wait to receive PASS action

        if(!recv_cmd(sd,"PASS", pass)){
        warnx("FAIL receiving password");
        return false;
    }

    // if credentials don't check denied login or confirm login

    if(check_credentials(user,pass)){
        if(send_ans(sd,MSG_230,user)){
            warnx("Failed sending login confirm");
            return false;
        }
        printf("User %s logged in.\n", user); 
        return true;
    }else{
        if(send_ans(sd,MSG_530)){
            warnx("Failed sending login denial");
        
        }  
        return false;
    }

}

/**
 *  function: execute all commands (RETR|QUIT)
 *  sd: socket descriptor
 **/

void operate(int sd, struct sockaddr_in * clientaddr) {
    char op[CMDSIZE], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';
        // check for commands send by the client if not inform and exit
        if(!recv_cmd(sd, op, param)){
            warnx("Annormally flow");
            continue;
        }       

        if (strcmp(op, "RETR") == 0) {
            retr(sd, clientaddr ,param);
        }else if(strcmp(op, "STOR")== 0){
            stor(sd, clientaddr, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // send goodbye and close connection
            send_ans(sd,MSG_221);
            close(sd);
            printf("User logged out.\n");
            break;
        } else {
            // invalid command
            warnx("Not supported commnad");
            // furute use

        }
    }
}

/** Funciones Auxiliares **/
bool direccion_puerto(char *string){
    bool verificacion = true;
    int i=0;
    while(*(string+i)!='\0'){
        if(!isdigit(*(string+i))) verificacion = false;
        i++;
    }
    if(atoi(string)<0||atoi(string)>65535) verificacion = false;
    return verificacion;
}


/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {

    // arguments checking
    if(argc!=2){
        errx(1, "Error en el numero de argumentos");
    }
    if(!direccion_puerto(argv[1])){
        errx(1, "Puerto Invalido");
    }
    // reserve sockets and variables space
    int msd, ssd; //master(server) - slave(cliente)
    struct sockaddr_in m_addr, s_addr;
    socklen_t s_addr_len;

    // create server socket and check errors

    msd = socket(AF_INET, SOCK_STREAM, 0);
    if(msd <0){
        errx(1, "CANT CREATE SERVER SOCKET");
    }

    // bind master socket and check errors

    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = INADDR_ANY;
    m_addr.sin_port = htons(atoi(argv[1])); // puerto del servidor


    if(bind(msd,(struct sockaddr *)&m_addr, sizeof(m_addr))<0){
        errx(2,"CANT BIND SOCKET");
    }

    // make it listen

    if(listen(msd, 10)<0){
        errx(3,"LISTEN ERROR");
    }

    signal(SIGCHLD,sig_handler);

    // main loop
    while (true) {
        // accept connectiones sequentially and check errors
        s_addr_len = sizeof(s_addr);
        ssd = accept(msd, (struct sockaddr *)&s_addr, &s_addr_len);
        if(ssd < 0){
            errx(4,"ACCEPT ERROR");
        }
        #if DEBUG
        printf("Cliente se conecto\n");
        #endif // DEBUG

        if(!fork()) {
            //c hild process

            // close master socket
            close(msd);
        

            // send hello

            send_ans(ssd,MSG_220);

            // operate only if authenticate is true

            if(authenticate(ssd)){
                operate(ssd, &s_addr);
            }else{
                warn("Connection closed");
            }
            close(ssd);
            exit(0);
        }
        close(ssd);
    }

    // close server socket
    close(msd);
    return 0;
}
