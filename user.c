#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

int main() {
    // 1. Crear pipes únicos basados en PID actual
    char pipe_out[50], pipe_in[50];
    sprintf(pipe_out, "user_%d_out", getpid());
    sprintf(pipe_in, "user_%d_in", getpid());
    
    // 2. Crear los FIFOs
    mkfifo(pipe_out, 0666);
    mkfifo(pipe_in, 0666);
    
    // 3. Abrir FIFOs
    int fd_out = open(pipe_out, O_WRONLY);
    int fd_in = open(pipe_in, O_RDONLY);
    
    if (fd_in < 0 || fd_out < 0) {
        perror("open");
        exit(1);
    }
    
    char msg[100];
    char msg_with_pid[200];
    printf("Usuario conectado (PID: %d). Escribe '/exit' para salir.\n", getpid());

    if (fork() == 0) {
        // Proceso hijo: solo escucha mensajes entrantes
        char buffer[256];
        while(1) {
            int n = read(fd_in, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("%s", buffer);
            } else if (n == 0) {
                break;  // Central se desconectó
            }
        }
        exit(0);
    } else {
        while(1) {
        //printf("Mensaje: ");
        
        if (fgets(msg, sizeof(msg), stdin) == NULL) {
            break;
        }
        
        if (strcmp(msg, "/exit\n") == 0) {
            // Cerrar file descriptors
            close(fd_in);
            close(fd_out);
            
            // Eliminar los pipes del directorio
            if (unlink(pipe_out) == -1) {
                perror("Error eliminando pipe_out");
            }
            if (unlink(pipe_in) == -1) {
                perror("Error eliminando pipe_in");
            }
            
            printf("Pipes eliminados. Usuario desconectado.\n");
            break;
        }
        
        if (strcmp(msg, "/clone\n") == 0) {
            printf("Creando un nuevo usuario...\n");
            if (fork() == 0) {
                // Proceso hijo: abrir nueva terminal
                system("gnome-terminal -- ./user &");
                exit(0);
            }
            continue;
        }

        if (strncmp(msg, "/report ", 8) == 0) {
            int reported_pid;
            if (sscanf(msg, "/report %d", &reported_pid) == 1) {
                snprintf(msg_with_pid, sizeof(msg_with_pid), "%d:REPORT:%d\n", getpid(), reported_pid);
                write(fd_out, msg_with_pid, strlen(msg_with_pid));
                printf("Usuario %d reportado.\n", reported_pid);
            } else {
                printf("Uso: /report <pid>\n");
            }
            continue;
        }
        
        // Enviar mensaje
        msg[strcspn(msg, "\n")] = 0;
        snprintf(msg_with_pid, sizeof(msg_with_pid), "%d:%s\n", getpid(), msg);
        write(fd_out, msg_with_pid, strlen(msg_with_pid));
    }
    }
    
    close(fd_in);
    close(fd_out);
    return 0;
}

