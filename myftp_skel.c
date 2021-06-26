#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFSIZE 512

#define MSG_550 "550 %s: no such file or directory\r\n"

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = read(sd,buffer,sizeof(BUFSIZE));
 
    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message); // se pordira usar strncpy
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
    if(write(sd, buffer,strlen(buffer)+1 )<0){
        warn("No puede escribir");
    }
    return;
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd,"USER",input);
    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    //331
    if(!recv_msg(sd,331,NULL)){
        warn("No recibio el 331 del servidor");
        return ;
    }

    // ask for password
    printf("password: ");
    input = read_input();

    // send the command to the server
    //230
    send_msg(sd,"PASS",input);
    // release memory
    free(input);

    // wait for answer and process it and check for errors
    //230
    if(!recv_msg(sd,230,NULL)){
        warn("Login incorrecto");
        exit(3);
        return ;
    }
    printf("Logueado Correctamente!");

}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    int dsd, dsd2; // data channel socket
    int puerto;
    char *ip = (char*)malloc(13*sizeof(char));
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    struct sockaddr_in taddr;
    socklen_t t_len = sizeof(taddr);
    
    getsockname(sd, (struct sockaddr *) &taddr, &t_len);

    puerto = 0; // ser coloca en 0 para que el SO elija un puerto libre

    // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(2, "Can't create socket");
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr, sizeof(addr)) < 0) errx(2,"Can't not bind");
    if(listen(dsd,1) < 0) errx(15, "Listen data channel error");

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);

    // check for the response
    if(!recv_msg(sd, 299, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsd2 = accept(dsd, (struct sockaddr*)&addr, &len);
    if (dsd2 < 0) {
       errx(16, "Accept data channel error");
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %d bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");

    // receive the file from data channel
    while(true) {
       if (f_size < BUFSIZE) r_size = f_size;
       recv_s = read(dsd, buffer, r_size);
       if(recv_s < 0) warn("receive error");
       fwrite(buffer, 1, r_size, file);
       if (f_size < BUFSIZE) break;
       f_size = f_size - BUFSIZE;
    }

    // close data channel
    close(dsd2);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;
}

void put(int sd, char *file_name){
    char buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    int dsd, dsd2; // data channel socket
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    struct sockaddr_in taddr;
    socklen_t t_len = sizeof(taddr);
    int puerto, bread;
    char *ip;
    char *file_data, *file_size;
    file_data = (char*)malloc(50*sizeof(char));
    file_size = (char*)malloc(25*sizeof(char));

    // open file to read and check
    ;
    if((file = fopen(file_name,"r")) == NULL){
        printf(MSG_550, file_name);
        return;
    }
    
    //file length
    fseek(file, 0L, SEEK_END);
    f_size = ftell(file);
    rewind(file);
    sprintf(file_size, "_%ld",f_size);

    puerto = 0;// se coloca en 0 para que seleccione un puerto random libre.

    // listen to data channel (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(2, "Can't create socket");
    taddr.sin_family = AF_INET;
    taddr.sin_addr.s_addr = INADDR_ANY;
    taddr.sin_port = htons(puerto); // se le setea 0 para que el SO elija el puerto libre.
    if (bind(dsd, (struct sockaddr *) &taddr, sizeof(taddr)) < 0) errx(2,"Can't not bind");
    if(listen(dsd,1) < 0) errx(5, "Listen data channel error");

    
    file_data=strcat(file_name,file_size);
    // send the STOR command to the server
    send_msg(sd, "STOR", file_data);

    // check for the response
    if(!recv_msg(sd, 299, buffer)) {
       close(dsd);
       return;
    }

    // accept new connection
    dsd2 = accept(dsd, (struct sockaddr*)&addr, &len);
    if (dsd2 < 0) {
       errx(6, "Accept data channel error");
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %d bytes", &f_size);


    // receive the file from data channel
    while(true) {
       
       bread = fread( buffer,1, BUFSIZE, file);
       if(bread > 0){
           send(dsd2,buffer,bread,0);
           sleep(1);
       }

       if (bread < BUFSIZE) break;
       
    }

    // close data channel
    close(dsd2);

    // close the file
    fclose(file);

    // receive the OK from the server
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // close listening socket
    close(dsd);

    return;

}





/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd,"QUIT",NULL);
    // receive the answer from the server
    
    if(!recv_msg(sd, 221, NULL))
        errx(7, "logout Incorrecto");
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }else if(strcmp(op, "put") == 0){
            param = strtok(NULL, " ");
            put(sd,param);
        }else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

/** Funciones Auxiliares **/


bool direccion_IP(char *string){
    char *token;
    bool verificacion = true;
    int contador=0,i;
    token = (char *) malloc(strlen(string)*sizeof(char));
    strcpy(token, string);
    token = strtok(token,".");

    while(token!=NULL){
        contador++;
        i=0;
        while(*(token+i)!='\0'){
            if(!isdigit(*(token+i))) verificacion = false;
            i++;
        }
        if(atoi(token)<0||atoi(token)>255) verificacion = false;
        token=strtok(NULL,".");
    }
    if(contador!=4) verificacion = false;
    free(token);

    return verificacion;
}

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


void get_ip_port(int sd,char *ip, int *port){
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    getsockname(sd, (struct sockaddr*) &addr, &len);    
    sprintf(ip, "%s", inet_ntoa(addr.sin_addr));

    *port = (uint16_t)ntohs(addr.sin_port);

}


/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;
    
    // arguments checking
    if(argc != 3)
    {
        //char *ip_svr = argv[1];
        // terminar checkeo
        errx(1, "Error en el numero de Argumentos");
    }
    if(!direccion_IP(argv[1]))
        errx(1, "IP invalida");
    if(!direccion_puerto(argv[2]))
        errx(1, "Puerto invalido");

    // create socket and check for errors
    sd = socket(AF_INET, SOCK_STREAM,0);
    if(sd<0){
        perror("No se pudo crear el socket");
        exit(1);
    }
    // set socket data    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr= inet_addr(argv[1]);//direccion servidor
    addr.sin_port = htons(atoi(argv[2]));// puerto servidor
     
    // connect and check for errors
    if(connect(sd,(struct sockaddr *)&addr, sizeof(addr))<0){
        perror("No se puudo conectar al servidor");
        exit(2);
    }
    // if receive hello proceed with authenticate and operate if not warning
    if(recv_msg(sd,220,NULL)){
        authenticate(sd);
        operate(sd);
    }else {
        warn("No recibio el OK del servidor");
    }
    
    // close socket
    close(sd);
    return 0;
}
