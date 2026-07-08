#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>   // Necesario para umask
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include "parser.h"
#define BUFFER_LENGTH 1024 //tamaño máximo de entrada
#define MAX_JOBS 20

typedef enum { RUNNING, STOPPED } JobStatus;

typedef struct {
    int id;             // ID del trabajo (1, 2...)
    pid_t pid;          // PID del último proceso de la línea
    char command[1024]; // Línea de comando original
    JobStatus status;   // Estado
} job_t;

job_t jobs_list[MAX_JOBS]; // Array global de jobs
int n_jobs = 0;

void execute(tline* line,char *raw_cmd);
int redirect(tline* line,int i);
int shell_command(tline* line);
void add_job(pid_t pid, char* cmd, JobStatus status);
void delete_job(int index);
void print_jobs();
void exec_bg(char **argv);

int main() {
    char buffer[BUFFER_LENGTH];
    char raw_cmd[BUFFER_LENGTH]; // Copia para guardar el texto original
    tline* line;

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    while (1) {
        printf("msh> ");
        fflush(stdout);//para evitar salidas inesperadas del buffer

        if (fgets(buffer,BUFFER_LENGTH, stdin)!=NULL) {

            buffer[strcspn(buffer, "\n")] = '\0';//eliminamos el salto de linea de fgets
	    strcpy(raw_cmd, buffer);//copiamos en raw_cmd el nombre del posible mandato para mostrarlo en jobs
            line=tokenize(buffer);//transformamos la cadena de caracteres leída a tline
            if (line != NULL) {
                if (line->ncommands == 0) {
                    continue;
                }

                if (shell_command(line) != 1) {
                    execute(line, raw_cmd);//Pasamos 'raw_cmd' para jobs
                }
            }
        } else {
            break; // Ctrl+D
        }

    }
    return 0;
}

void execute(tline* line,char *raw_cmd) {
    int i;
    int n;
    pid_t pid;
    int p[2];         // Pipe actual
    int fd_in;   // Descriptor de lectura heredado del mandato anterior
    int status;

    n = line->ncommands;
    fd_in = -1;

    // BUCLE PRINCIPAL: Recorremos cada mandato de la línea
    for (i = 0; i < n; i++) {
        if (i < n - 1) {//creamos un pipe al siguiente mandato, si no es el último mandato
            if (pipe(p) == -1) {
                perror("Error creando pipe");
                exit(1);
            }
        }

        pid = fork();

        if (pid < 0) {//error en el fork
            perror("Error en fork");
            exit(1);
        }

        if (pid == 0) {//hijo

            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            if (i > 0) {//si no es el primero, cambiamos su entrada(de entrada estandar al pipe del anterior)
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }

            if (i < n - 1) {//si no es el último, cambiamos su salida
                dup2(p[1], STDOUT_FILENO);
                close(p[0]); // El hijo no lee de este pipe
                close(p[1]);
            }

            //redirecciones(solo se pueden con el primero y el último)
            if (redirect(line, i) == -1) {
                exit(1); // Si falla abrir fichero, el hijo muere
            }


            if (line->commands[i].filename == NULL) {//si no encuentra el mandato, salta un mensaje de error
                fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
                exit(1);
            }
            //ejecutamos el mandato
            execvp(line->commands[i].argv[0], line->commands[i].argv);

            // Si sigue hasta aquí, significa que ha habido un error en execvp
            perror("Error en execvp");
            exit(1);
        }

        //padre(pid>0)
        if (i > 0) {
            close(fd_in); // Ya no necesitamos leer del pipe viejo
        }

        //Preparación del pipe actual para la siguiente vuelta
        if (i < n - 1) {
            fd_in = p[0];  // Guardamos el lado de lectura para el siguiente hijo
            close(p[1]);   //cerramos escritura(Le llega EOF al hijo)
        }
    }

    if (line->background==1) {
        // Background. No esperamos .
        // Añadimos el último PID a la lista de jobs como RUNNING
        add_job(pid, raw_cmd, RUNNING);
    }
    else {
        //Foreground.
        // Esperamos al último hijo, pero activamos WUNTRACED para detectar Ctrl+Z
        waitpid(pid, &status, WUNTRACED);

        if (WIFSTOPPED(status)) {
            // El usuario pulsó Ctrl+Z. El proceso se detuvo, no murió.
            // Lo añadimos a la lista como STOPPED.
            printf("\n[%d]+ Stopped\t%s\n", n_jobs + 1, raw_cmd);
            add_job(pid, raw_cmd, STOPPED);
        }

        // Limpiamos zombies de los procesos intermedios del pipe (si hubo)
        for (i = 0; i < n - 1; i++) wait(NULL);
    }
}
int redirect(tline* line, int i) {//funcion que se encarga de las redirecciones. Devuelve 0 si funciona correctamente y -1 si hay error
    int fd;
    int n;

    n=line->ncommands;

    if (i==0){//primer mandato
        if (line->redirect_input!=NULL) {//redireccion de entrada(<): leer de fichero
            fd=open(line->redirect_input,O_RDONLY); //abrimos el fichero en "modo lectura"(O_RDONLY)
            if (fd==-1) {//se ha producido un error al abrir fichero
                fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));//fichero, descripcion error
                return -1;
            }
            dup2(fd,STDIN_FILENO);//cambiar entrada estandar(0) por el fichero
            close(fd);//cerramos el fichero
        }
    }

    if (i==n-1){//último mandato

        if (line->redirect_output!=NULL) {//redireccion de salida(>): escribir en fichero
            fd=open(line->redirect_output,O_WRONLY|O_CREAT|O_TRUNC,0660);//escribir(WRONLY),crear(CREAT),sobreescribir(TRUNC),permisos en caso de crear(0660)si no crea lo ignora
            if (fd==-1) {//se ha producido un error al abrir fichero
                fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));//fichero, descripcion error
                return -1;
            }
            dup2(fd,STDOUT_FILENO);//cambiar salida estandar(1) por el fichero
            close(fd);
        }

        if (line->redirect_error!=NULL) {//redireccion de error(>&): escribir error en fichero
            fd=open(line->redirect_error,O_WRONLY|O_CREAT|O_TRUNC,0660);//escribir(WRONLY),crear(CREAT),sobreescribir(TRUNC),permisos en caso de crear(0660)si no crea lo ignora
            if (fd==-1) {//se ha producido un error al abrir fichero
                fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));//fichero, descripcion error
                return -1;
            }
            dup2(fd,STDERR_FILENO);//Cambiamos la salida de error(2) por el fichero
            close(fd);
        }
    }

    return 0;
}

int shell_command(tline* line){//Funcion devuelve 1 si es un comando interno y 0 si no lo es. Si es un mandato interno, lo ejecuta.
    char* command;
    int argc;
    char* path;
    char cwd[BUFFER_LENGTH];
    mode_t mask;
    int new_mask_val;

    command = line->commands[0].argv[0];
    argc = line->commands[0].argc;

    if (strcmp(command, "exit")==0) {
        exit(0);//devuelve 1
    }
    if (strcmp(command, "cd")==0) {
        path=NULL;

        if (line->ncommands>1) {//hay más comandos aparte de cd [ruta](cd no admite pipes)
            //fprintf(stderr, "No se ha podido ejecutar\n");
            return 1;//no podemos ejecutarlo, pero devolvemos 1 porque hay un cd, para que luego no se intente ejecutar con execute
        }

        if (argc==1) {//cd (no tiene argumentos por lo que usa la ruta predeterminada(HOME))
            path=getenv("HOME");
            if (path==NULL) {
                fprintf(stderr, "No se ha encontrado HOME\n");
                return 1;
            }
        }
        else if (argc==2) {//cd [ruta]
            path=line->commands[0].argv[1];
            if (path==NULL) {
                fprintf(stderr, "No se ha encontrado la ruta\n");
                return 1;
            }
        }
        if (path!=NULL) {
            if (chdir(path)==-1) {
                perror("Error en chdir");
                return 1;
            }
            if (getcwd(cwd, BUFFER_LENGTH)==NULL) {
                perror("Error en getcwd");
            }else {
                printf("%s\n", cwd);
            }
            return 1;
        }
    }
    if (strcmp(command, "jobs") == 0) {//jobs
        print_jobs();
        return 1;
    }

    if (strcmp(command, "bg") == 0) {//bg
        exec_bg(line->commands[0].argv);
        return 1;
    }


    if (strcmp(command, "umask") == 0) {//umask

        // no admite pipes
        if (line->ncommands > 1) {
            fprintf(stderr, "umask: no se permite con pipes\n");
            return 1;
        }

        // umask sin argumentos -> Mostrar máscara actual
        if (line->commands[0].argc == 1) {
            // umask(0) devuelve la anterior máscara, pero la restauramos con la misma para que se mantenga en la actual
            mask = umask(0);
            umask(mask);
            printf("%04o\n", mask); // Imprimir en octal (ej: 0022)
        }
        // umask con argumento (número octal)
        else if (line->commands[0].argc == 2) {
            // strtol base 8 para convertir string "0174" a número
            new_mask_val = strtol(line->commands[0].argv[1], NULL, 8);
            umask(new_mask_val);
            printf("%04o\n", new_mask_val);
        } else {
            printf("Uso: umask [octal]\n");
        }
        return 1;
    }

    return 0;
}

void add_job(pid_t pid, char* cmd, JobStatus status) {
    if (n_jobs < MAX_JOBS) {
        jobs_list[n_jobs].id = n_jobs + 1;
        jobs_list[n_jobs].pid = pid;
        jobs_list[n_jobs].status = status;
        strcpy(jobs_list[n_jobs].command, cmd);
        n_jobs++;

        //Si se lanza en background directamente, mostramos PID
        if (status == RUNNING) {
            printf("[%d] %d\n", n_jobs, pid);
        }
        else {
            printf("Error: Máximo de trabajos alcanzado\n");
        }
    }
}
void delete_job(int index) {
    int i;
    for (i = index; i < n_jobs - 1; i++) {
        jobs_list[i] = jobs_list[i + 1];
        jobs_list[i].id = i + 1; // Reajustar IDs para que sean consecutivos
    }
    n_jobs--;
}
void print_jobs() {
    int i;
    int status;
    pid_t res;

    for (i = 0; i < n_jobs; i++) {
        // Comprobamos si el proceso ha terminado
        // WNOHANG: No bloquea, solo mira si hay cambios
        res = waitpid(jobs_list[i].pid, &status, WNOHANG);

        if (res == jobs_list[i].pid) {
            // El trabajo terminó, lo borramos de la lista y ajustamos el índice
            delete_job(i);
            i--;
        } else {
            // Sigue vivo, imprimimos su estado
            printf("[%d]+ %s\t%s\n",jobs_list[i].id,(jobs_list[i].status == RUNNING) ? "Running" : "Stopped",jobs_list[i].command);
        }
    }
}
void exec_bg(char **argv) {
    int i;
    int job_idx = -1;
    int target_id;

    // Si no hay argumentos, buscar el último parado
    if (argv[1] == NULL) {
        for (i = n_jobs - 1; i >= 0; i--) {
            if (jobs_list[i].status == STOPPED) {
                job_idx = i;
                break;
            }
        }
    } else {
        // Si hay argumento (id), buscarlo.
        target_id = atoi(argv[1]);
        for (i = 0; i < n_jobs; i++) {
            if (jobs_list[i].id == target_id) {
                job_idx = i;
                break;
            }
        }
    }

    if (job_idx != -1) {
        // Enviar señal SIGCONT para reanudar el proceso
        if (kill(jobs_list[job_idx].pid, SIGCONT) == 0) {
            jobs_list[job_idx].status = RUNNING;
            // Indicar el trabajo que se está ejecutando
            printf("[%d]+ %s &\n", jobs_list[job_idx].id, jobs_list[job_idx].command);
        } else {
            perror("Error al hacer bg");
        }
    } else {
        printf("bg: no existe ese trabajo o no hay trabajos detenidos\n");
    }
}
