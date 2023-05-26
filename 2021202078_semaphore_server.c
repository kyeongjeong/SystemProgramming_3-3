///////////////////////////////////////////////////////////////////////////////////////
// File Name    : 2021202078_ipc_server.c                                            //
// Date         : 2023/05/24                                                         //
// OS           : Ubuntu 16.04.5 Desktop 64bits                                      //
// Author       : Choi Kyeong Jeong                                                  //
// Student ID   : 2021202078                                                         //
// --------------------------------------------------------------------------------- //
// Title        : System Programming Assignment 3-2                                  //
// Descriptions : Improve Assignment 3-1 by utilizing shared memory and threads.     //
//                Instead of storing history in a child process, store it in shared  //
//                memory. Access to the shared memory should be done through threads.//
//                Additionally, the number of idle processes should be adjusted      //
//                based on given maximum and minimum values.                         //                                             
///////////////////////////////////////////////////////////////////////////////////////

#define _GNU_SOURCE
#define FNM_CASEFOLD  0x10
#define DEFAULT_ARRAY_LENGTH 10000
#define URL_LEN 256
#define BUFSIZE 1024
#define PORTNO 40000
#define SEND_ARRAY_LEN 99999

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <ctype.h>
#include <fnmatch.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

struct ClientInfo {

    int No;
    int Port;
    int PID;
    char IP[BUFSIZE];
    char TIME[BUFSIZE];
};

int request;
int* IdleProcessCount;
struct ClientInfo client_info[10];
static pid_t *pids;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

pid_t child_make(int socketfd);
void parentSignalHandler(int sig);
void childSignalHandler(int sig);
void child_main(int socketfd);
void saveConnectHistory(struct sockaddr_in client_addr);
void sendResponse(char* url, char* response_header, int client_fd);
void listDirFiles(int a_hidden, int l_format, char* filename, char* sendArray);
void getAbsolutePath(char *inputPath, char *absolutePath);
void removeDuplicateChars(char* str);
void sortByNameInAscii(char **fileList, int fileNum, int start);
void printPermissions(mode_t mode, char* sendArray);
void printType(struct stat fileStat, char* sendArray);
void findColor(char* fileName, char* color);
void printAttributes(struct stat fileStat, char *color, char* sendArray);
int compareStringUpper(char* fileName1, char* fileName2);
int writeLsPage(char* url, char* sendArray);
int isAccesible(char* inputIP, char* response_header, int client_fd);
int isExist(int client_fd, char* response_header, char* url);

///////////////////////////////////////////////////////////////////////////////////////
// main                                                                              //
// --------------------------------------------------------------------------------- //
// Input:                                                                            //
// output:                                                                           //
// purpose: Create a socket to ensure smooth communication between the server and    //
//          client. Depending on the client's request, the server sends the          //
//          corresponding results to the web browser, which includes exception       //
//          handling for each error situation.                                       //
///////////////////////////////////////////////////////////////////////////////////////
int main() {

    struct sockaddr_in server_addr, client_addr; // 서버 및 클라이언트의 주소
    struct tm *lt; // 시간 정보를 담는 구조체 포인터
    struct sigaction sigAct; // 시그널 처리를 위한 구조체
    sigset_t mask; // 시그널 집합
    time_t t; // 시간을 저장하는 변수
    char timeBuf[BUFSIZE]; // 현재 시간을 저장할 문자열 배열
    char line[BUFSIZE]; // httpd.conf 한 줄을 저장할 배열
    int socket_fd, client_fd; // 소켓 및 클라이언트의 파일 디스크립터
    int MaxChilds, MaxIdleNum, MinIdleNum, StartProcess, MaxHistory; // 최대 자식프로세스 개수, 최대 Idle프로세스 개수, 최소 Idle프로세스 개수, 시작 시 자식프로세스 개수, 최대 history 개수
    int shm_id; //공유 메모리 id
    request = 0; // 클라이언트와의 연결 횟수

    FILE *file = fopen("httpd.conf", "r"); // httpd.conf 파일을 read로 open
   
    while (fgets(line, sizeof(line), file) != NULL) { //파일 read
        sscanf(line, "MaxChilds: %d", &MaxChilds); // MaxChilds 값 저장
        sscanf(line, "MaxIdleNum: %d", &MaxIdleNum); // MaxIdleNum 값 저장
        sscanf(line, "MinIdleNum: %d", &MinIdleNum); // MinIdleNum 값 저장
        sscanf(line, "StartProcess: %d", &StartProcess); // StartProcess 값 저장
        sscanf(line, "MaxHistory: %d", &MaxHistory); // MaxHistory 값 저장
    }

    sigAct.sa_handler = childSignalHandler; //SIGUSR1 Signaling Function Registration
    sigemptyset(&sigAct.sa_mask); 
    sigAct.sa_flags = 0;
    sigaction(SIGUSR1, &sigAct, NULL);

    if((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) { //소켓 생성
        printf("Server : Can't open stream socket\n"); //소켓 생성 실패 시
        return 0; //프로그램 종료
    }

    bzero((char*)&server_addr, sizeof(server_addr)); //server_addr 메모리 블록을 0으로 초기화
    server_addr.sin_family = AF_INET; //주소 체계를 IPv4로 설정
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //모든 IP 주소를 허용
    server_addr.sin_port = htons(PORTNO); //포트 번호 설정

    if(bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) { //소켓에 IP 주소와 포트 번호 할당
        printf("Server : Can't bind local address\n"); //소켓 할당 실패 시 
        return 0; //프로그램 종료
    }
    else {
        time(&t);  // 현재 시간을 t 변수에 저장
        lt = localtime(&t);  // t 변수를 이용해 로컬 시간 구조체 포인터를 얻음
        strftime(timeBuf, 1024, "%c", lt);  // 로컬 시간을 timeBuf 문자열에 포맷팅하여 저장
        printf("[%s] Server is started\n", timeBuf);  // timeBuf 문자열을 이용하여 서버 시작 메시지 출력
    }

    if(listen(socket_fd, 5) == -1) { // 클라이언트와의 연결 대기
        printf("Server : can't listen\n"); //연결 대기 실패 시
        return 0; //프로그램 종료
    }

    if((shm_id = shmget((key_t)PORTNO, BUFSIZE, IPC_CREAT|0666)) == -1) { // 공유메모리 생성  
        printf("shmget fail\n"); // 생성 실패 시
        return 0; // 프로그램 종료
    }

    if((IdleProcessCount = (int*)shmat(shm_id, (void*)0, 0)) == (void*)-1) { // IdleProcessCount를 공유메모리에 추가
        printf("shmat fail\n"); // 추가 실패 시
        return 0; //프로그램 종료
    }

    *IdleProcessCount = 0; // Idle Process 수를 0으로 초기화

    signal(SIGALRM, parentSignalHandler); //alarm에 대한 action 설정
    signal(SIGINT, parentSignalHandler); //init에 대한 action 설정
    alarm(10); //10초 뒤 alarm

    pids = (pid_t *)malloc(StartProcess * sizeof(pid_t)); //pid 배열을 10 크기로 동적할당
    int addrlen = sizeof(client_addr); //client_addr의 사이즈를 addrlen에 저장

    for(int i = 0; i < StartProcess; i++) //pids 배열을 돌면서
        pids[i] = child_make(socket_fd);  //child_make 함수를 실행(fork)

    while(1) {

        pthread_mutex_lock(&counter_mutex);

        pthread_mutex_unlock(&counter_mutex);
    }

    close(socket_fd); //소켓 연결 해제
    return 0; //프로그램 종료
}

///////////////////////////////////////////////////////////////////////////////////////
// child_make                                                                        //
// --------------------------------------------------------------------------------- //
// Input: int socket_fd -> socket descpriptor                                        //
// output:                                                                           //
// purpose: After performing the fork(), if the process is the parent process, it    //
//          returns the process ID (PID), and if it is the child process, it         //
//          executes the child_main function.                                        //
///////////////////////////////////////////////////////////////////////////////////////
pid_t child_make(int socket_fd) {

    pid_t pid; //fork를 할 pid
    pid = fork();

    if (pid < 0) { // 프로세스 생성 실패 시
        printf("Server : fork failed\n"); //실패 문구 출력
        return pid; // pid 반환
    } 
    else if(pid > 0) //부모 프로세스가 호출된 경우
        return pid; //pid 반환
    
    child_main(socket_fd); //자식 프로세스가 호출된 경우 child 프로세스의 main 함수 호출
}

///////////////////////////////////////////////////////////////////////////////////////
// child_main                                                                        //
// --------------------------------------------------------------------------------- //
// Input: int socket_fd -> socket descpriptor                                        //
// output:                                                                           //
// purpose: Depending on the client's request, the server sends the corresponding    //
//          results to the web browser, which includes exception  handling for each  //
//          error situation.                                                         //
///////////////////////////////////////////////////////////////////////////////////////
void child_main(int socket_fd)
{
    struct sockaddr_in client_addr; // 클라이언트의 주소를 저장하는 구조체 변수
    struct tm *lt; // 시간 정보를 저장하는 구조체 포인터
    time_t t; // 시간 정보를 저장하는 변수
    char curTime[BUFSIZE]; // 임시 문자열 버퍼
    unsigned int len; // 클라이언트 주소의 길이
    int client_fd; // 클라이언트의 파일 디스크립터
    int shm_id; //공유 메모리 id

    pthread_mutex_lock(&counter_mutex); //공유메모리 접근 제한

    if((shm_id = shmget((key_t)PORTNO, BUFSIZE, IPC_CREAT|0666)) == -1) { // 공유메모리 생성  
        printf("shmget fail\n"); // 생성 실패 시
        exit(0); // 프로그램 종료
    }

    if((IdleProcessCount = (int*)shmat(shm_id, (void*)0, 0)) == (void*)-1) { // IdleProcessCount를 공유메모리에 추가
        printf("shmat fail\n"); // 추가 실패 시
        exit(0); //프로그램 종료
    }

    time(&t); // 현재 시간 저장
    lt = localtime(&t); // 현재 시간의 로컬 시간 정보 저장
    strftime(curTime, 1024, "%c", lt); // 시간 정보를 문자열로 변환하여 temp에 저장
    printf("[%s] %ld process is forked.\n", curTime, (long)getpid()); // 포크된 프로세스의 정보 출력
    (*IdleProcessCount)++; // Idle 프로세스 수 증가
    printf("[%s] IdleProcessCount : %d\n", curTime, *IdleProcessCount); // Idle 프로세스 수 출력

    signal(SIGUSR1, childSignalHandler); // SIGUSR1 시그널 핸들러 등록
    signal(SIGTERM, childSignalHandler); // SIGTERM 시그널 핸들러 등록

    while(1) {
        
        struct in_addr inet_clinet_address; //클라이언트의 주소
        char response_header[BUFSIZE] = {0, }; //응답 메세지 헤더
        char buf[BUFSIZE] = {0, }; //클라이언트의 요청 메세지
        char tmp[BUFSIZE] = {0, }; //요청 메세지 복사 후 토큰으로 분리하기 위한 변수
        char url[URL_LEN] = {0, }; //클라이언트의 요청 URL 주소
        char method[20] = {0, }; //클라이언트의 HTTP 요청 메소드
        char* tok = NULL; //토큰으로 분리할 문자열 포인터

        len = sizeof(client_addr); //클라이언트의 주소 길이 저장
        client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &len); //클라이언트로부터 요청 받음

        if(client_fd < 0) {
            printf("Server : accept failed\n"); //연결 요청 실패 시
            return; //프로그램 종료
        }

        inet_clinet_address.s_addr = client_addr.sin_addr.s_addr; // 클라이언트 주소 정보 저장
        if (read(client_fd, buf, BUFSIZE) == 0) // 읽어올 수 있는 데이터가 없는 경우
            continue; // 무시 후 다시 연결 받기
        
        strcpy(tmp, buf);
        tok = strtok(tmp, " "); // HTTP 요청 메소드 받음
        strcpy(method, tok);    // method에 tok 내용 저장
        if (strcmp(method, "GET") == 0) { // GET 요청인 경우
            tok = strtok(NULL, " "); // 요청한 URL 주소 받음
            strcpy(url, tok);        // url에 tok 내용 저장
        }
        removeDuplicateChars(url); //'/' 문자 중복 제거

        if (strcmp(url, "/favicon.ico") == 0) // url이 /favicon.ico인 경우
            continue; // 무시 후 다시 연결 받기

        if(isAccesible(inet_ntoa(client_addr.sin_addr), response_header, client_fd) == 0) //접근 권한이 없는 IP일 경우
            continue; // 무시 후 다시 연결 받기

        if(isExist(client_fd, response_header, url) == 1) //존재하지 않는 디렉토리에 대한 접근
            continue; // 무시 후 다시 연결 받기

        (*IdleProcessCount)--;
        time(&t);  // 현재 시간을 t 변수에 저장
        lt = localtime(&t);  // t 변수를 이용해 로컬 시간 구조체 포인터를 얻음
        strftime(curTime, 1024, "%c", lt);  // 로컬 시간을 temp 문자열에 포맷팅하여 저장
        printf("========= New Client ============\n[%s]\nIP : %s\nPort : %d\n=================================\n", curTime, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port)); // 연결된 클라이언트의 IP 주소 및 포트 번호 출력
        printf("[%s] IdleProcessCount : %d\n", curTime, *IdleProcessCount);
        sendResponse(url, response_header, client_fd); //아닌 경우, response 메세지 입력 및 출력

        ++request; // 누적 접속 기록 개수 증가
        saveConnectHistory(client_addr);

        close(client_fd); //client file descriptor 닫기
        (*IdleProcessCount)++; //클라이언트에 연결되지 않은 프로세스 카운트 증가
        time(&t);  // 현재 시간을 t 변수에 저장
        lt = localtime(&t);  // t 변수를 이용해 로컬 시간 구조체 포인터를 얻음
        strftime(curTime, 1024, "%c", lt);  // 로컬 시간을 temp 문자열에 포맷팅하여 저장
        printf("====== Disconnected Client ======\n[%s]\nIP : %s\nPort : %d\n=================================\n", curTime, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port)); // 연결 해제된 클라이언트의 IP 주소 및 포트 번호 출력
        printf("[%s] IdleProcessCount : %d\n", curTime, *IdleProcessCount);
    }
    pthread_mutex_unlock(&counter_mutex); //공유메모리 접근 제한 해제
    return; //프로그램 종료
}

///////////////////////////////////////////////////////////////////////////////////////
// parentSignalHandler                                                               //
// --------------------------------------------------------------------------------- //
// Input: int sig -> The generated signals through signal()                          //
// output:                                                                           //
// purpose: Receiving signals generated by the parent process and executing actions  //
//          based on their types.                                                    //
///////////////////////////////////////////////////////////////////////////////////////
void parentSignalHandler(int sig) {

    if(sig == SIGALRM) { //SIGALRM 신호를 받은 경우

        printf("\n========= Connection History ===================================\n"); //history title
        printf("No.\tIP\t\tPID\tPORT\tTIME\n"); //history의 세부 항목
        for(int i = 0; i < *IdleProcessCount; i++) //자식 프로세스 만큼 반복
            kill(pids[i], SIGUSR1); //자식 프로세스에 신호 전달
        alarm(10); //다시 10초 알람 설정
    }

    if(sig == SIGINT) { //SIGINT 신호를 받은 경우

        struct tm *lt;              // 현재 시간을 저장할 구조체 포인터
        time_t t;                   // 시간 정보를 저장할 변수
        char curTime[BUFSIZE];      // 시간 문자열을 저장할 배열
        int *status;                // 자식 프로세스의 종료 상태를 저장할 포인터


        for (int i = 0; i < *IdleProcessCount; i++) //자식 프로세스 만큼 반복
            kill(pids[i], SIGTERM); //자식 프로세스에 신호 전달
        for (int i = 0; i < *IdleProcessCount; i++) //자식 프로세스 만큼 반복
            waitpid(pids[i], status, WNOHANG); //terminate된 자식 프로세스 수거
        for (int i = 0; i < *IdleProcessCount; i++) { //자식 프로세스 만큼 반복
            time(&t);  // 현재 시간을 t 변수에 저장
            lt = localtime(&t);  // t 변수를 이용해 로컬 시간 구조체 포인터를 얻음
            strftime(curTime, 1024, "%c", lt);  // 로컬 시간을 curTime 문자열에 포맷팅하여 저장
            printf("[%s] %d process is terminated\n", curTime, pids[i]); //자식 프로세스의 종료를 알림
        }

        time(&t);  // 현재 시간을 t 변수에 저장
        lt = localtime(&t);  // t 변수를 이용해 로컬 시간 구조체 포인터를 얻음
        strftime(curTime, 1024, "%c", lt);  // 로컬 시간을 curTime 문자열에 포맷팅하여 저장
        printf("[%s] Server is terminated\n", curTime); //서버(부모 프로세스)의 종료를 알림
        exit(0);
    }
}

///////////////////////////////////////////////////////////////////////////////////////
// childSingalHandler                                                                //
// --------------------------------------------------------------------------------- //
// Input: int sig -> The generated signals through signal()                          //
// output:                                                                           //
// purpose: Receiving signals generated by the child process and executing actions   //
//          based on their types.                                                    //
///////////////////////////////////////////////////////////////////////////////////////
void childSignalHandler(int sig) {

    if(sig == SIGUSR1) { //SIGUSR1 신호를 받은 경우

        int n = request; //출력 개수 설정
        if (request > 2) //연결되었던 기록이 10개가 넘는 경우
            n = 2; //10개만 출력되도록 설정

        for (int i = n - 1; i >= 0; i--) //출력 개수만큼 반복
            printf("%d\t%s\t%d\t%d\t%s\n", client_info[i].No, client_info[i].IP, client_info[i].PID, client_info[i].Port, client_info[i].TIME); //클라이언트와의 연결 기록 출력
    }

    if(sig == SIGTERM) //SIGTERM 신호를 받은 경우
        exit(0); //자식 프로세스 종료
}

///////////////////////////////////////////////////////////////////////////////////////
// saveConnectHistory                                                                //
// --------------------------------------------------------------------------------- //
// Input: struct sockaddr_in client_addr -> The address of the connected client.     //
// output:                                                                           //
// purpose: Store the 10 most recent connection records. The connection record       //
//          should include the client connection number, IP address, port number,    //
//          process IP executed, and the time when the server and client were        //
//          connected.                                                               //
///////////////////////////////////////////////////////////////////////////////////////
void saveConnectHistory(struct sockaddr_in client_addr) {

    struct tm *lt;              // 현재 시간을 저장할 구조체 포인터
    time_t t;                   // 시간 정보를 저장할 변수
    char curTime[BUFSIZE];         // 시간 문자열을 저장할 배열
    int n;                      // 클라이언트 정보 배열의 인덱스 변수

    if (request < 11) //연결 개수가 10개 이하일 경우
        n = request - 1; //request-1번째 배열로 인덱스 설정
    else { //연결 개수가 10개를 넘어갈 경우
        n = 9; //인덱스를 9로 설정
        for(int i = 0; i < 10; i++) { //구조체 배열을 돌면서 한 칸씩 앞으로 당기기
            client_info[i].No = client_info[i + 1].No; // 클라이언트의 연결된 순서를 이동
            client_info[i].Port = client_info[i + 1].Port; // 클라이언트의 IP 주소를 이동
            client_info[i].PID = client_info[i + 1].PID; // 클라이언트 요청 번호를 이동
            strcpy(client_info[i].IP, client_info[i + 1].IP); // 클라이언트의 프로세스 ID를 이동
            strcpy(client_info[i].TIME, client_info[i + 1].TIME); // 클라이언트의 포트 번호를 이동
        }
    }
    
    strcpy(client_info[n].IP, inet_ntoa(client_addr.sin_addr));       // 클라이언트의 IP 주소를 저장
    client_info[n].No = request;                                      // 클라이언트 요청 번호를 저장
    client_info[n].PID = getpid();                                    // 클라이언트의 프로세스 ID를 저장
    client_info[n].Port = ntohs(client_addr.sin_port);                // 클라이언트의 포트 번호를 저장
    
    time(&t); // 현재 시간 저장
    lt = localtime(&t); // 현재 시간의 로컬 시간 정보 저장
    strftime(curTime, 1024, "%c", lt); // 시간 정보를 문자열로 변환하여 curTime에 저장
    strcpy(client_info[n].TIME, curTime); // 클라이언트의 연결 시간을 저장   
}

///////////////////////////////////////////////////////////////////////////////////////
// isAccessible                                                                      //
// --------------------------------------------------------------------------------- //
// Input: char* inputIP -> The IP address that the client is attempting to access.   //
//        char* response_header -> Response message header from the server           //
//        int client_fd -> File descriptor of the client                             //
// output:                                                                           //
// purpose: Classify whether the address entered as a URL is a directory, plain file // 
//          , or image, then perform file read to create a response message and send //
//          it with a header.                                                        //
///////////////////////////////////////////////////////////////////////////////////////
int isAccesible(char* inputIP, char* response_header, int client_fd) {

    FILE* file = fopen("accessible.usr", "r"); //허용 가능한 IP가 적힌 파일 open
    char accessIDs[DEFAULT_ARRAY_LENGTH]; //IP를 읽어올 문자열
    char error_message[DEFAULT_ARRAY_LENGTH]; //서버의 에러 응답 메세지

    char *token = strtok(inputIP, "/");
    strcpy(inputIP, token);

    while(fgets(accessIDs, DEFAULT_ARRAY_LENGTH, file) != NULL) { //파일의 끝까지 IP 읽어오기
        
        if(fnmatch(accessIDs, inputIP, 0) == 0) //현재 접근 시도하는 IP가 접근 가능 권한이 있을 경우
            return 1; //1을 반환
    }

    sprintf(error_message, "<h1>Access denied!</h1><br>"
                           "<h3>Your IP : %s</h3><br>"
                           "you have no permission to access this web server.<br>"
                           "HTTP 403.6 - Forbidden: IP address reject<br>", inputIP); // 에러 메세지 설정
    sprintf(response_header, "HTTP/1.0 200 OK\r\nServer: 2023 web server\r\nContent-length: %ld\r\nContent-Type: text/html\r\n\r\n", strlen(error_message)+1); // 헤더 메세지 설정

    write(client_fd, response_header, strlen(response_header)+1); // 응답 메세지 헤더 write
    write(client_fd, error_message, strlen(error_message)+1);     // 에러 응답 메세지 write
    fclose(file); //파일 close
    return 0; //현재 접근 시도하는 IP가 접근 가능 권한이 없을 경우 0 반환
}

///////////////////////////////////////////////////////////////////////////////////////
// isexist                                                                           //
// --------------------------------------------------------------------------------- //
// Input: char* url -> The URL address requested by the client                       //
//        char* response_header -> Response message header from the server           //
//        int client_fd -> File descriptor of the client                             //
// output:                                                                           //
// purpose: If the input path is a directory, check if it exists. If it doesn't      //
//          exist, display an error message.                                         //
///////////////////////////////////////////////////////////////////////////////////////
int isExist(int client_fd, char* response_header, char* url) {

    struct stat st;
    char dirPath[DEFAULT_ARRAY_LENGTH] = {'\0', }; //절대경로를 받아올 배열
    getAbsolutePath(url, dirPath); //dirPath에 url의 절대경로를 받아오기
    removeDuplicateChars(dirPath); //dirPath에서 연속으로 중복된 /제거
    
    if((lstat(dirPath, &st) == -1) || (((st.st_mode & S_IFMT)==S_IFDIR) && (opendir(dirPath) == NULL))) { //url이 디렉토리이고, 존재하지 않는다면

        char error_message[DEFAULT_ARRAY_LENGTH]; //서버의 에러 응답 메세지

        sprintf(error_message, "<h1>Not Found</h1><br>The request URL %s was not found on this server<br>HTTP 404 - Not Page Found", url); //에러 메세지 설정
        sprintf(response_header, "HTTP/1.0 404\r\nServer: 2023 web server\r\nContent-length: %ld\r\nContent-Type: text/html\r\n\r\n", strlen(error_message)+1); //헤더 메세지 설정
        
        write(client_fd, response_header, strlen(response_header) + 1); //응답 메세지 헤더 write
        write(client_fd, error_message, strlen(error_message) + 1); //에러 응답 메세지 write
        return 1; //존재하지 않는 디렉토리임을 알리면서 반환
    }

    return 0; //존재하는 디렉토리임을 알리면서 반환
}

///////////////////////////////////////////////////////////////////////////////////////
// sendResponse                                                                      //
// --------------------------------------------------------------------------------- //
// Input: char* url -> The URL address requested by the client                       //
//        char* response_header -> Response message header from the server           //
//        int client_fd -> File descriptor of the client                             //
// output:                                                                           //
// purpose: Classify whether the address entered as a URL is a directory, plain file // 
//          , or image, then perform file read to create a response message and send //
//          it with a header.                                                        //
///////////////////////////////////////////////////////////////////////////////////////
void sendResponse(char* url, char* response_header, int client_fd) {

    FILE *file; //url로 열 파일
    struct dirent *dir; //디렉토리 정보를 담을 구조체
    struct stat st; //파일 정보를 담을 구조체
    int count = 0; //파일의 길이, 파일의 존재 여부
    char content_type[30];   // MIME TYPE을 저장할 배열
    char Apath[DEFAULT_ARRAY_LENGTH]; //절대경로를 불러올 배열
    char *response_message = NULL; //서버의 응답 메세지
    char *sendArray = (char *) malloc(SEND_ARRAY_LEN * sizeof(char)); //ls 결과를 담을 배열

    getAbsolutePath(url, Apath); //Apath에 url의 절대 경로 받아오기
    lstat(Apath, &st); //절대경로로 파일 정보 불러오기

    //이미지 파일인 경우
    if ((fnmatch("*.jpg", url, FNM_CASEFOLD) == 0) || (fnmatch("*.png", url, FNM_CASEFOLD) == 0) || (fnmatch("*.jpeg", url, FNM_CASEFOLD) == 0)) 
        strcpy(content_type, "image/*"); //content-type을 image/*로 설정

    else { //디렉토리 또는 일반 파일인 경우

        if((st.st_mode & S_IFMT)==S_IFDIR) { //디렉토리인 경우 또는 root path인 경우
            strcpy(content_type, "text/html"); //content-type을 text/html로 설정
            writeLsPage(url, sendArray); //html 파일에 ls 결과 입력
        }
        else
            strcpy(content_type, "text/plain"); //content-type을 text/plain으로 설정
    }

    if (strcmp(content_type, "text/html") == 0) //디렉토리 주소를 입력받은 경우
        count = strlen(sendArray) + 1; //ls 결과를 담은 배열의 크기 +1 만큼 count 설정
    
    else { //파일을 읽어야 하는 경우(ls 결과를 불러오지 않음)
        if(strcmp(content_type, "text/plain") == 0) 
            file = fopen(Apath, "r"); //일반 파일을 읽어오는 경우
        else
            file = fopen(Apath, "rb"); //이미지를 읽어오는 경우

        fseek(file, 0, SEEK_END); //파일의 끝부분으로 이동
        count = ftell(file); //파일의 크기를 count에 저장
        fseek(file, 0, SEEK_SET); //파일의 가장 앞으로 이동
        rewind(file); //파일 포인터를 가장 앞쪽으로 이동
    }

    response_message = (char *)malloc((sizeof(char)) * (count+1)); //count+1의 크기만큼 response_message 크기 지정
    
    if (strcmp(content_type, "text/html") == 0) //디렉토리 주소를 입력받은 경우
        strcpy(response_message, sendArray); //ls 결과를 reponse_message에 저장

    else {

        if (strcmp(content_type, "image/*") == 0) //이미지 파일인 경우
            fread(response_message, 1, count+1, file); //이미지 바이너리 파일 read 내용을 응답 메세지에 저장
    
        else //일반 파일인 경우
            fread(response_message, sizeof(char), count+1, file); //파일 read 내용을 응답 메세지에 저장

        fclose(file); //file close
    }

    sprintf(response_header, "HTTP/1.0 200 OK\r\nServer: 2023 web server\r\nContent-length: %d\r\nContent-Type: %s\r\n\r\n", count+1, content_type); //서버 응답 메세지 헤더 설정
    write(client_fd, response_header, strlen(response_header)); //서버 응답 메세지 헤더 출력
    write(client_fd, response_message, count+1); //서버 응답 메세지 출력
}

///////////////////////////////////////////////////////////////////////////////////////
// writeLsPage                                                                       //
// --------------------------------------------------------------------------------- //
// Input: char* url -> The URL address requested by the client                       //
// output:                                                                           //
// purpose: If the requested file is a directory, input the ls results for the files //
//          in the directory into an HTML document.                                  //
///////////////////////////////////////////////////////////////////////////////////////
int writeLsPage(char* url, char* sendArray) {
    
    char exePath[DEFAULT_ARRAY_LENGTH] = {'\0', }; //working directory의 절대 경로
    char dirPath[DEFAULT_ARRAY_LENGTH] = {'\0', }; //절대경로를 받아올 배열

    getcwd(exePath, DEFAULT_ARRAY_LENGTH); //curPath에 현재 실행 위치 불러오기
    exePath[strlen(exePath)] = '/'; //curPath 제일 뒤에 /붙이기
    exePath[strlen(exePath)] = '\0';
    sprintf(sendArray, "<!DOCTYPE html>\n<html>\n<head>\n"); //html 파일 선언
    sprintf(sendArray, "%s<title>%s</title>\n</head>\n<body>\n", sendArray, exePath); //절대경로로 title 설정

    if((url[1] == '\0') || (strcmp(url, exePath) == 0)) { //root path인 경우

        sprintf(sendArray, "%s<h1>Welcome to System Programming Http</h1>\n<br>\n", sendArray); //header 작성
        char currentPath[10] = "."; //현재 경로
        listDirFiles(0, 1, currentPath, sendArray); //현재 디렉토리 하위 파일 출력
    }

    else { //root path가 아닌 경우

        sprintf(sendArray, "%s<h1>System Programming Http</h1>\n<br>\n", sendArray); //header 작성   
        getAbsolutePath(url, dirPath); //dirPath에 url의 절대경로를 받아오기
        
        if(opendir(dirPath) == NULL) //url이 디렉토리가 아니라면 
            return 1; //함수 종료

        listDirFiles(1, 1, url, sendArray); //url이 디렉토리라면 listDirFiles() 실행
    }
    return 0; //함수 종료
}

///////////////////////////////////////////////////////////////////////////////////////
// listDirFiles                                                                      //
// --------------------------------------------------------------------------------- //
// Input: int a_hidden -> option -a                                                  //
//        int l_format -> option -l                                                  //
//        char* filename -> file name that provided                                  //
// output:                                                                           //
// purpose: Prints sub-files in the directory specified by the filename argument     //
//          based on the options                                                     //
///////////////////////////////////////////////////////////////////////////////////////
void listDirFiles(int a_hidden, int l_format, char* filename, char* sendArray) {

    DIR *dirp; //dir 포인터
    struct dirent *dir; //dirent 구조체
    struct stat fileStat; //파일 속성정보 저장할 구조체
    int fileNum = 0, realfileNum = 0; //파일의 개수
    char timeBuf[80]; //시간 정보
    int total = 0; //총 블락 수
    char exePath[DEFAULT_ARRAY_LENGTH]; //현재 실행파일의 위치(working directory)
    char accessPath[DEFAULT_ARRAY_LENGTH]; //디렉토리 하위 파일들의 경로
    char dirPath[DEFAULT_ARRAY_LENGTH] = {'\0', }; //ls 결과를 보여줄 디렉토리의 절대경로
    int* isHidden = (int*)calloc(fileNum, sizeof(int)); //히든파일 여부 판별

    getAbsolutePath(filename, dirPath); //dirPath에 filename의 절대 경로 불러오기
    dirp = opendir(dirPath); //절대경로로 opendir

    getcwd(exePath, DEFAULT_ARRAY_LENGTH); //exePath에 현재 실행 위치 불러오기
    if(strcmp(dirPath, exePath) == 0) //현재 실행 위치와 열려고 하는 파일의 경로가 같은 경우
        a_hidden = 0; //히든 파일이 ls 결과로 출력되지 않게 설정

    while((dir = readdir(dirp)) != NULL) { //디렉토리 하위 파일들을 읽어들임

        strcpy(accessPath, dirPath); //입력받은 경로 불러오기
        strcat(accessPath, "/"); // /를 붙이고
        strcat(accessPath, dir->d_name); //읽어온 파일명 붙이기
        lstat(accessPath, &fileStat); //파일의 절대 경로로 lstat() 호출

        if(a_hidden == 1 || dir->d_name[0] != '.') {
            total += fileStat.st_blocks; //옵션(-a)에 따라 total 계산           
            ++realfileNum; //총 파일개수 count
        }
        ++fileNum; //총 파일개수 count
    }
    
    rewinddir(dirp); //dirp 처음으로 초기화

    char **fileList = (char**)malloc(sizeof(char*) * (fileNum+1)); //동적 할당
    for(int i = 0; i < fileNum; i++) { //파일 개수만큼 반복
        
        fileList[i] = (char*)malloc(sizeof(char) * 300); //동적 할당
        dir = readdir(dirp); //디렉토리 내 파일 읽기
        strcpy(fileList[i], dir->d_name); //fileList에 파일명 저장
    }

    sortByNameInAscii(fileList, fileNum, 0); //아스키 코드순으로 정렬
    sprintf(sendArray, "%s<b>Directory path: %s</b><br>\n", sendArray, dirPath); //파일 경로 출력
    sprintf(sendArray, "%s<b>total : %d</b>\n", sendArray, (int)(total/2));

    if(realfileNum == 0) { //만약 히든 파일을 제외한 파일이 없다면
        sprintf(sendArray, "%s<br><br>\n", sendArray);
        return; //함수 종료
    }

    sprintf(sendArray, "%s<table border=\"1\">\n<tr>\n<th>Name</th>\n", sendArray); // 테이블 생성
    sprintf(sendArray, "%s<th>Permissions</th>\n", sendArray);                      // 권한 열 생성
    sprintf(sendArray, "%s<th>Link</th>\n", sendArray);                             // 링크 열 생성
    sprintf(sendArray, "%s<th>Owner</th>\n", sendArray);                            // 소유자 열 생성
    sprintf(sendArray, "%s<th>Group</th>\n", sendArray);                            // 소유 그룹 열 생성
    sprintf(sendArray, "%s<th>Size</th>\n", sendArray);                             // 사이즈 열 생성
    sprintf(sendArray, "%s<th>Last Modified</th>\n</tr>", sendArray);               // 마지막 수정날짜 열 생성

    for (int i = 0; i < fileNum; i++) { // 파일 개수만큼 반복

        if ((a_hidden == 0) && fileList[i][0] == '.') // 옵션 -a 여부에 따라 파일속성 출력
            continue;

        strcpy(accessPath, dirPath); //입력받은 경로 불러오기
        strcat(accessPath, "/"); // /를 붙이고
        strcat(accessPath, fileList[i]); //읽어온 파일명 붙이기
        removeDuplicateChars(accessPath); // accessPath에서 중복된(연속된) /이 있으면 제거
        lstat(accessPath, &fileStat); // 파일 속성정보 불러옴

        char color[20] = {'\0',}; // 파일의 색상 저장할 배열
        findColor(accessPath, color); // 색 찾기

        int n = strlen(exePath); //n에 현재 실행 위치(경로)의 길이 저장
        char* p = strstr(accessPath, exePath); //accessPath에서 현재 실행 위치(경로)의 시작점을 p로 설정
        while (p = strstr(accessPath, exePath)) {
            char *q = p + n; //현재 실행 위치(경로)가 끝나는 부분을 q로 설정
            while (*q) //q 위치부터 앞으로 오면서
                *p++ = *q++; //현재 실행 위치를 제거하여 상대 경로로 만들기
            *p = '\0';
        }

        sprintf(sendArray, "%s<tr style=\"%s\">\n", sendArray, color); //ls의 결과 색 출력
        sprintf(sendArray, "%s<td><a href=%s>%s</a></td>", sendArray, accessPath, fileList[i]); // 파일 이름 및 링크 출력

        printAttributes(fileStat, color, sendArray); // 속성 정보 출력
    }
    sprintf(sendArray, "%s</table>\n<br>\n", sendArray); //table 닫기
}

///////////////////////////////////////////////////////////////////////////////////////
// getAbsolutePath                                                                   //
// --------------------------------------------------------------------------------- //
// Input: char* inputPath -> Path input                                              //
//        char* absolutePath -> path that made asolute                               //
// output:                                                                           //
// purpose: Finds the absolute path of the inputted directory or file.               //
///////////////////////////////////////////////////////////////////////////////////////
void getAbsolutePath(char *inputPath, char *absolutePath) {

    getcwd(absolutePath, DEFAULT_ARRAY_LENGTH); //현재 경로 받아옴

    if(inputPath[0] != '/') //입력이 절대경로가 아닌 파일이고, /로 시작하지 않을 때 ex) A/*
        strcat(absolutePath, "/"); // /을 제일 뒤에 붙여줌
    
    if(strstr(inputPath, absolutePath) != NULL) //입력받은 경로가 현재 경로를 포함할 때 ex)/home/Assignment/A/*
        strcpy(absolutePath, inputPath); //입력받은 경로로 절대경로 덮어쓰기
    else
        strcat(absolutePath, inputPath); //현재 경로에 입력받은 경로 이어붙이기
        
    removeDuplicateChars(absolutePath); //연속된(중복) /가 있다면 제거
}

///////////////////////////////////////////////////////////////////////////////////////
// removeDuplicateChars                                                              //
// --------------------------------------------------------------------------------- //
// Input: char* str -> String to remove the forward slash '/'                        //
// output:                                                                           //
// purpose: If there are consecutive duplicate forward slashes '/' in the input      //
//          string "str", remove all but one occurrence of the forward slash.        //
///////////////////////////////////////////////////////////////////////////////////////
void removeDuplicateChars(char* str) {

    int i, j, len = strlen(str); //만약 /가 중복될 경우 제거하는 과정
    for (i = j = 0; i < len; i++) { //절대 경로를 순회하면서
        if (i > 0 && str[i] == '/' && str[i-1] == '/') {
            // do nothing
        } 
        else {
            str[j++] = str[i]; //'/'를 제거하기 위해 한 칸씩 앞으로 당기기
        }
    }
    str[j] = '\0'; //마지막 문자를 null로 하여 문자열 마무리
}

///////////////////////////////////////////////////////////////////////////////////////
// printAttributes                                                                   //
// --------------------------------------------------------------------------------- //
// Input: struct stat fileStat -> Save information about a file (such as file size   //
//                                , owner, permissions, etc.)                        //
// output:                                                                           //
// purpose: Prints the attributes of the file using the information from the given   //
//          name of struct stat object                                               //
///////////////////////////////////////////////////////////////////////////////////////
void printAttributes(struct stat fileStat, char *color, char* sendArray) {
    
    char timeBuf[80]; //시간 정보 받아올 변수
    printType(fileStat, sendArray); //파일 유형
    printPermissions(fileStat.st_mode, sendArray); //허가권
    sprintf(sendArray, "%s<td>%ld</td>", sendArray, fileStat.st_nlink); //링크 수
    sprintf(sendArray, "%s<td>%s</td><td>%s</td>", sendArray, getpwuid(fileStat.st_uid)->pw_name, getgrgid(fileStat.st_gid)->gr_name); //파일 소유자 및 파일 소유 그룹

    sprintf(sendArray, "%s<td>%ld</td>", sendArray, fileStat.st_size); // 파일 사이즈

    strftime(timeBuf, sizeof(timeBuf), "%b %d %H:%M", localtime(&fileStat.st_mtime)); // 수정된 날짜 및 시간 불러오기
    sprintf(sendArray, "%s<td>%s</td>", sendArray, timeBuf); // 수정된 날짜 및 시간 출력

    sprintf(sendArray, "%s</tr>\n", sendArray);
}

///////////////////////////////////////////////////////////////////////////////////////
// sortByNameInAscii                                                                 //
// --------------------------------------------------------------------------------- //
// Input: char **fileList -> An array containing file names.                         //
//        int fileNum -> size of fileList                                            //
// output:                                                                           //
// purpose: Sort the filenames in alphabetical order (ignoring case) without the dot //
///////////////////////////////////////////////////////////////////////////////////////
void sortByNameInAscii(char **fileList, int fileNum, int start)
{
    int* isHidden = (int*)calloc(fileNum, sizeof(int)); //hidden file인지 판별 후 저장
    
    for (int i = start; i < fileNum; i++) { //파일리스트 반복문 실행
         if ((fileList[i][0] == '.') && (strcmp(fileList[i], ".") != 0) && (strcmp(fileList[i], "..") != 0)) { //hidden file인 경우
            isHidden[i] = 1; //파일명 가장 앞의 . 제거
            for (int k = 0; k < strlen(fileList[i]); k++) //파일 글자수 반복
                fileList[i][k] = fileList[i][k + 1]; //앞으로 한 칸씩 땡기기
        }
    }

    for (int i = start; i < (fileNum - 1); i++) { // 대소문자 구분 없는 알파벳 순으로 정렬
        for (int j = i + 1; j < fileNum; j++) { //bubble sort
            if (compareStringUpper(fileList[i], fileList[j]) == 1) {
            //만약 첫 문자열이 둘째 문자열보다 작다면
                char *temp = fileList[i]; // 문자열 위치 바꾸기
                fileList[i] = fileList[j];
                fileList[j] = temp;

                int temp2 = isHidden[i]; //히든파일인지 저장한 배열도 위치 바꾸기
                isHidden[i] = isHidden[j];
                isHidden[j] = temp2;
            }
        }
    }

    for (int i = start; i < fileNum; i++) { //리스트 반복문 돌리기
        if(isHidden[i] == 1) { //hidden file인 경우
            for(int k = strlen(fileList[i]); k >= 0; k--) //파일 길이만큼 반복
                fileList[i][k+1] = fileList[i][k]; //뒤로 한 칸씩 보내기
            fileList[i][0] = '.'; //파일명 가장 앞에 . 다시 추가
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////
// compareStringUpper                                                                //
// --------------------------------------------------------------------------------- //
// Input: char* fileName1 -> The first string to compare                             //
//        char* fileName2 -> The second string to compare                            //
// output: 0 -> No need to swap positions, 1 -> Need to swap positions               //
// purpose: Comparing two strings based on uppercase letters to determine which one  //
//          is greater.                                                              //
///////////////////////////////////////////////////////////////////////////////////////
int compareStringUpper(char* fileName1, char* fileName2) {
    
    char* str1 = (char*)calloc(strlen(fileName1)+1, sizeof(char)); //비교할 첫 번째 문자열
    char* str2 = (char*)calloc(strlen(fileName2)+1, sizeof(char)); //비교할 두 번째 문자열

    for(int i = 0; i < strlen(fileName1); i++) //첫 번째 문자열을 돌면서
        str1[i] = toupper(fileName1[i]); //모두 대문자로 전환

    for(int i = 0; i < strlen(fileName2); i++) //두 번째 문자열을 돌면서
        str2[i] = toupper(fileName2[i]); //모두 대문자로 전환

    if((strcmp(str1, ".") == 0 || strcmp(str1, "..") == 0) && (strcmp(str2, ".") != 0)) //위치를 바꿀 필요가 없는 경우
        return 0; //0 반환

    else if((strcmp(str2, ".") == 0 || strcmp(str2, "..") == 0) && (strcmp(str1, ".") != 0)) //위치를 바꿀 필요가 있는 경우
        return 1; //1 반환

    else if(strcmp(str1, str2) > 0) //위치를 바꿀 필요가 있는 경우
        return 1; //1 반환
    
    return 0; //위치를 바꿀 필요가 없는 경우 0 반환
}

///////////////////////////////////////////////////////////////////////////////////////
// printPermissions                                                                  //
// --------------------------------------------------------------------------------- //
// Input: mode_t mode -> represents the permission information of a file.            //
//        char* sendArray -> An array to send the results of a function.             //
// output:                                                                           //
// purpose: Printing file permissions for user, group, and others.                   //
///////////////////////////////////////////////////////////////////////////////////////
void printPermissions(mode_t mode, char* sendArray) {

    sprintf(sendArray, "%s%c", sendArray, (mode & S_IRUSR) ? 'r' : '-'); //user-read
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IWUSR) ? 'w' : '-'); //user-write
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IXUSR) ? 'x' : '-'); //user-execute
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IRGRP) ? 'r' : '-'); //group-read
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IWGRP) ? 'w' : '-'); //group-write
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IXGRP) ? 'x' : '-'); //group-execute
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IROTH) ? 'r' : '-'); //other-read
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IWOTH) ? 'w' : '-'); //other-write
    sprintf(sendArray, "%s%c", sendArray, (mode & S_IXOTH) ? 'x' : '-'); //other-execute

    sprintf(sendArray, "%s</td>", sendArray);
}

///////////////////////////////////////////////////////////////////////////////////////
// printType                                                                         //
// --------------------------------------------------------------------------------- //
// Input: struct stat fileStat -> Save information about a file (such as file size   //
//                                , owner, permissions, etc.)                        //
//        char* sendArray -> An array to send the results of a function.             //
// output:                                                                           //
// purpose: Printing file type(regular file, directory, symbolic link, etc.)         //
///////////////////////////////////////////////////////////////////////////////////////
void printType(struct stat fileStat, char* sendArray) {

    sprintf(sendArray, "%s<td>", sendArray);

    switch (fileStat.st_mode & S_IFMT) {
    case S_IFREG: //regular file
        sprintf(sendArray, "%s-", sendArray);
        break;
    case S_IFDIR: //directory
        sprintf(sendArray, "%sd", sendArray);
        break;
    case S_IFLNK: //symbolic link
        sprintf(sendArray, "%sl", sendArray);
        break;
    case S_IFSOCK: //socket
        sprintf(sendArray, "%ss", sendArray);
        break;
    case S_IFIFO: //FIFO(named pipe)
        sprintf(sendArray, "%sp", sendArray);
        break;
    case S_IFCHR: //character device
        sprintf(sendArray, "%sc", sendArray);
        break;
    case S_IFBLK: //block device
        sprintf(sendArray, "%sb", sendArray);
        break;
    default:
        sprintf(sendArray, "%s?", sendArray); //unknown
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////
// findColor                                                                         //
// --------------------------------------------------------------------------------- //
// Input: char* fileName -> File name to determine the file type                     //
//        char* color -> Array to store colors based on the file type                //
// output:                                                                           //
// purpose: Detects the file type based on the inputted file name and stores the     //
//          corresponding color in an array.                                         //
///////////////////////////////////////////////////////////////////////////////////////
void findColor(char* fileName, char* color) {

    struct stat fileStat; //파일 속성정보
    lstat(fileName, &fileStat); //파일 경로로 속성 정보 받아오기

    if ((fileStat.st_mode & S_IFMT) == S_IFDIR)      // 디렉토리일 경우
        strcpy(color, "color: Blue");                // 파랑 출력
    else if ((fileStat.st_mode & S_IFMT) == S_IFLNK) // 심볼릭 링크일 경우
        strcpy(color, "color: Green");               // 초록 출력
    else                                             // 그 외 파일들의 경우
        strcpy(color, "color: Red");                 // 빨강 출력
}