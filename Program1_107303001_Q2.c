#include <stdio.h>
#include <unistd.h>
/*
int dup (int oldfd);
dup()用來複制引數oldfd 所指的檔案描述詞, 並將它返回。此新的檔案描述詞和引數oldfd 指的是同一個檔案, 共享所有的鎖定、讀寫位置和各項許可權或旗標
返回值：當複製成功時, 則返回最小及尚未使用的檔案描述詞。若有錯誤則返回-1, errno 會存放錯誤程式碼

int dup2(int odlfd, int newfd);
複制引數oldfd 所指的檔案描述詞, 並將它拷貝至引數newfd 後一塊返回
返回值：當複製成功時, 則返回最小及尚未使用的檔案描述詞。若有錯誤則返回-1, errno 會存放錯誤程式碼
*/
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

void type_prompt();
char** read_command();
void launch(char**);
void take_token(char cmd[], char *token[], char *file[]);
void exec(char cmd[]);

#define MAX_SIZE 1024

int commandSize;

int main()
{
    /*
        Linux shell
        1. print prompt
        2. read command
        3. build child process
        4. execute command
    */

    char** cmd = malloc(MAX_SIZE * sizeof(char*));

    while(1)
    {
        type_prompt();
        cmd = read_command();
        launch(cmd);
    }

    return 0;
}

void type_prompt()
{
    struct passwd *user;
    user = getpwuid(getuid());

    char hostname[128] = {'\0'};
    gethostname(hostname, sizeof(hostname));

    char path[128] = {'\0'};
    getcwd(path,sizeof(path));

    char prompt = '$';
    if (geteuid() == 0)
        prompt = '#';

    printf("%s@%s:%s%c ", user->pw_name, hostname, path, prompt);
}

char** read_command()
{
    char line[MAX_SIZE];
    char** command = malloc(MAX_SIZE*sizeof(char*));
    fgets(line, MAX_SIZE, stdin);

    int i = 0; //指令個數
    char *str = NULL, *saveptr = NULL;
    char *enter = NULL;

    /* 根據 | 分割指令 */
    for(i=0, str=line; ; i++, str=NULL)
    {
        /* char *strtok_r(char *str, const char *delim, char **saveptr)
            saveptr用於存放指示下次掃描位置的指標 */
        command[i] = strtok_r(str, "|", &saveptr);
        if(command[i] == NULL)
        {
            /* char *strrchr(const char *str, int c)
            搜索最後出現的字符串中的字符c，返回一個指針，指向str中最後一次出現的字符 */
            enter = strrchr(command[i-1], '\n');
            *enter = ' ';   //替換末尾\n
            break;
        }
    }
    commandSize = i; //回傳給全域變數commandSize

    return command;
}

void launch(char** command)
{
    int exit_status;
    int fd[1024][2]; //pipe
    int index=0;
    int save_stdin, save_stdout;
    
    /* 儲存標準輸入輸出 */
    save_stdin = dup(STDIN_FILENO); 
    save_stdout = dup(STDOUT_FILENO);

    if(commandSize > 1) //如果有piping
    {
        if(pipe(fd[0]) < 0) //當前pipe
        {
            perror("pipe");
            exit(1);
        }
    }

    while(command[index] != NULL)
    {
        pid_t PID = fork();

        if(PID < 0)
        {
            printf("fork failed\n");
        }
        else if(PID == 0) //Child process
        {
            if(index > 0) //若有上一條指令，讀上一pipe並關閉寫端
            {
                close(fd[index-1][1]);
                dup2(fd[index-1][0], STDIN_FILENO);
            }
            if(command[index+1] != NULL) //若有下一條，寫當前pipe並關閉讀端
            {
                close(fd[index][0]);
                dup2(fd[index][1], STDOUT_FILENO);
            }
            else //當前指令是最後一條，恢復標準輸出
            {
                dup2(save_stdout, STDOUT_FILENO);
            }
            exec(command[index]);
        }
        else //Parents process waits for child process
        {
            if(command[index+1] != NULL && command[index+2] != NULL)
            {
                if(pipe(fd[index+1]) < 0) //當前pipe
                {
                    perror("pipe");
                    exit(1);
                }
            }

            if(index > 0) //兩次fork後關閉上一條指令的父程序pipe讀寫
            {
                close(fd[index-1][0]); //順序：fork子1,fork子2,關閉父讀寫
                close(fd[index-1][1]); //這個if寫在index++後面會阻塞子程序
            }
            //printf("[Parent] Waiting for child\n");
            wait(&exit_status);
            //printf("[Parent] Child process finished\n");
            index++;
        }
    }


}

void take_token(char cmd[], char *token[], char *file[])
{
    int i;
    char *op;
    char *str = NULL, *saveptr = NULL;
    int fd, std_fileno, file_mode;

    if( (op = strrchr(cmd, '<')) != NULL )
    {
        std_fileno = STDIN_FILENO; //標準輸入
        file_mode = O_RDONLY; //O_RDONLY: 以唯讀模式開啟
    }
    else if( (op = strrchr(cmd, '>')) != NULL )
    {
        std_fileno = STDOUT_FILENO; //標準輸出
        file_mode = O_WRONLY | O_CREAT | O_TRUNC;
        /*
            O_WRONLY: 以唯寫模式開啟
            O_CREAT: 如果指定的檔案不存在，就建立一個
            O_TRUNC: 若開啟的檔案存在且是一般檔案，開啟後就將其截短成長度為 0
        */
    }


    if(op) //如果有redirection
    {
        *op = '\0'; //redirection替換為'\0'
        *file = strtok_r((op+1), " ", &saveptr); //此步驟回傳檔名
        fd = open(*file, file_mode, 0666); //0666: 權限
        if(fd < 0)
        {
            perror("open");
            exit(1);
        }

        dup2(fd, std_fileno);
    }

    for(i=0, str=cmd, saveptr=NULL; ; i++, str=NULL) //根據空格分割指令
    {
        token[i] = strtok_r(str, " ", &saveptr);
        if(token[i] == NULL)
            break;
    }
}

void exec(char cmd[])
{
    char *tokens[MAX_SIZE];
    char *file;

    take_token(cmd, tokens, &file);

    execvp(tokens[0], tokens);
    perror(tokens[0]);
}
