#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <sys/select.h>
#include <signal.h>     
#include <sys/types.h> 
#include <sys/stat.h> 

#define BUFFER_SIZE 256

typedef struct {
    int pid;
    int fd_in;
    int fd_out; 
    int active;
    int error_count;
    char pipe_in_name[50];
    char pipe_out_name[50];
} User;

int add_user(User **users, int *user_count, int *capacity, int pid) {
    // verificar que haya espacio
    if (*user_count >= *capacity) {
        *capacity += 10;
        User *temp = realloc(*users, (*capacity) * sizeof(User));
        if (temp == NULL) {
            printf("Error al aumentar el tamaño de usuarios\n");
            return -1;
        }
        *users = temp;
    }
    
    // inicializar al usuario nuevo
    User *user = &(*users)[*user_count];
    user->pid = pid;
    user->error_count = 0; 
    user->active = 0;      
    
    sprintf(user->pipe_in_name, "user_%d_out", pid);
    sprintf(user->pipe_out_name, "user_%d_in", pid);
    
    user->fd_in = open(user->pipe_in_name, O_RDONLY | O_NONBLOCK);
    user->fd_out = open(user->pipe_out_name, O_WRONLY | O_NONBLOCK);
    
    if (user->fd_in < 0 || user->fd_out < 0) {
        user->error_count++;
        printf("Error count %d\n", user->error_count);
        
        if (user->error_count >= 5) {
            printf("Error abriendo pipes para usuario %d. Demasiados errores, eliminando usuario.\n", pid);
            user->active = 0;
            
            if (user->fd_in >= 0) close(user->fd_in);
            if (user->fd_out >= 0) close(user->fd_out);
            
            unlink(user->pipe_in_name);
            unlink(user->pipe_out_name);
        }
        return -1;
    }
    
    user->active = 1;
    (*user_count)++;
    printf("Usuario %d conectado\n", pid);
    return 0;
}

// funcion para escanear nuevos pipes/usuarios
void scan_for_new_pipes(User users[], int *user_count) {
    DIR *dir = opendir(".");
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "user_") && strstr(entry->d_name, "_out")) {
            int pid;
            if (sscanf(entry->d_name, "user_%d_out", &pid) == 1) {
                int found = 0;
                for (int i = 0; i < *user_count; i++) {
                    if (users[i].pid == pid && users[i].active) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    add_user(&users, user_count, pid);
                }
            }
        }
    }
    closedir(dir);
}

// enviar los mensajes a todos los usuarios
void broadcast_message(User users[], int *user_count, char *message, int sender_pid) {
    for (int i = 0; i < *user_count; i++) {
        if (users[i].active && users[i].pid != sender_pid) {
            write(users[i].fd_out, message, strlen(message));
        }
    }
}

// funcion para eliminar usuarios
/* void kick_user(int user_index) {
    User *user = &users[user_index];
    
    printf("MATANDO proceso %d por exceso de reportes\n", user->pid);
    
    // matar el proceso
    if (kill(user->pid, SIGTERM) == 0) {
        printf("Usuario %d eliminado del sistema\n", user->pid);
    } else {
        // si no funciona sigterm, usar sigkill
        perror("Error matando proceso");
        kill(user->pid, SIGKILL);
    }
    
    // limpiar pipes
    unlink(user->pipe_in_name);
    unlink(user->pipe_out_name);
    
    user->active = 0;
} */

//manejar errores
void handle_report(User users[], int *user_count, int reported_pid, int fd_reportes_out, int fd_reportes_in) {
    for (int i = 0; i < *user_count; i++) {
        if (users[i].pid == reported_pid && users[i].active) {
            // enviar reporte al proceso de reportes
            char report_msg[100];
            snprintf(report_msg, sizeof(report_msg), "%d\n", reported_pid);
            write(fd_reportes_out, report_msg, strlen(report_msg));

            // leer respuesta del proceso de reportes
            char buffer[100];
            int n = read(fd_reportes_in, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                printf("Central recibio: %s", buffer);         
            }       
        }
    }
}

int main() {
    // se inicializa el arreglo de usuarios
    User *users = malloc(10 * sizeof(User));
    int user_count = 0;

    fd_set read_fds;
    struct timeval timeout;
    char buffer[BUFFER_SIZE];
    int max_fd;
    
    // abrir los pipes hacia el proceso de reportes
    if (access("central_to_reportes", F_OK) == -1) {
        // el fifo no existe, crearlo
        mkfifo("central_to_reportes", 0666);
        mkfifo("reportes_to_central", 0666);
    }

    int fd_reportes_out = open("central_to_reportes", O_WRONLY | O_NONBLOCK);
    int fd_reportes_in = open("reportes_to_central", O_RDONLY | O_NONBLOCK);

    printf("Central iniciado. Escaneando usuarios...\n");
    while (1) {
        // escaneo constante por nuevos pipes
        scan_for_new_pipes(users, &user_count);
        
        // Preparar select
        FD_ZERO(&read_fds);
        max_fd = 0;
        
        for (int i = 0; i < user_count; i++) {
            if (users[i].active) {
                FD_SET(users[i].fd_in, &read_fds);
                if (users[i].fd_in > max_fd) {
                    max_fd = users[i].fd_in;
                }
            }
        }
        
        // Timeout para escanear nuevos pipes
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity > 0) {
            // ver cual usuario mando mensaje
            for (int i = 0; i < user_count; i++) {
                if (users[i].active && FD_ISSET(users[i].fd_in, &read_fds)) {
                    int n = read(users[i].fd_in, buffer, sizeof(buffer) - 1);
                    if (n > 0) {
                        buffer[n] = '\0';
                        
                        // ver si es reporte
                        if (strstr(buffer, "REPORT:") != NULL) {
                            int reported_pid;
                            if (sscanf(buffer, "%*d:REPORT:%d", &reported_pid) == 1) {
                                printf("Usuario %d reportado por %d\n", reported_pid, users[i].pid);
                                handle_report(users, &user_count, reported_pid, fd_reportes_out, fd_reportes_in);
                            }
                        } else {
                            // caso que no sea reporte, enviar mensaje
                            printf("- %s", buffer);
                            broadcast_message(users, &user_count, buffer, users[i].pid);
                        }
                    } else if (n == 0) {
                        printf("Usuario %d desconectado\n", users[i].pid);
                        close(users[i].fd_in);
                        close(users[i].fd_out);
                        users[i].active = 0;
                    }
                }
            }
        }
    }
    
    return 0;
}