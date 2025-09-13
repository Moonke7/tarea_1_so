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

typedef struct {
    int pid;
    int report_count;
} Report;

// funcion para eliminar usuarios
void kick_user(int user_pid) {
    printf("MATANDO proceso %d por exceso de reportes\n", user_pid);

    // matar el proceso
    if (kill(user_pid, SIGTERM) == 0) {
        printf("Usuario %d eliminado del sistema\n", user_pid);
    } else {
        // si no funciona sigterm, usar sigkill
        printf("Error matando proceso\n");
        kill(user_pid, SIGKILL);
    }

    // eliminar sus pipes
    char pipe_out[50], pipe_in[50];
    sprintf(pipe_out, "user_%d_out", user_pid);
    sprintf(pipe_in, "user_%d_in", user_pid);
    unlink(pipe_out);
    unlink(pipe_in);
}

int main(){
    printf("Proceso de reportes iniciado\n");   

    // crear fifos de conexion con la central en caso de que no existan
    if (access("central_to_reportes", F_OK) == -1) {
        // el fifo no existe, crearlo
        mkfifo("central_to_reportes", 0666);
        mkfifo("reportes_to_central", 0666);
    }

    // abrir los fifos 
    int fd_in = open("central_to_reportes", O_RDONLY); 
    int fd_out = open("reportes_to_central", O_WRONLY);

    if (fd_out < 0 || fd_in < 0) {
        printf("Error abriendo pipes\n");
        exit(1);
    }

    char buffer[100];
    Report *reports = malloc(10 * sizeof(Report));
    int reports_count = 0;
    int max_reports = 10;

    while(1) {
        int n = read(fd_in, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            
            printf("Central envio: %s", buffer);
            
            // procesar el mensaje
            int reported_pid = atoi(buffer);
            printf("PID reportado: %d\n", reported_pid);

            // buscar si ya existe el reporte
            int found = 0;
            int index_reported = -1;
            
            for (int i = 0; i < reports_count; i++) {
                if (reports[i].pid == reported_pid) {
                    reports[i].report_count++;
                    found = 1;
                    index_reported = i;
                    printf("Usuario %d ya reportado antes. Total: %d\n", reported_pid, reports[i].report_count);
                    break;
                }
            }

            if (!found) {
                // verificar que haya espacio
                if (reports_count >= max_reports) {
                    max_reports += 10;
                    reports = realloc(reports, max_reports * sizeof(Report));
                    if (reports == NULL) {
                        printf("Error al aumentar el tamaño de reportes\n");
                        exit(1);
                    }
                    printf("Espacio ampliado para reportes (nuevo límite: %d)\n", max_reports);
                }

                // agregar el nuevo reporte al final
                reports[reports_count].pid = reported_pid;
                reports[reports_count].report_count = 1;
                index_reported = reports_count;
                reports_count++;
            }

            // obtener número actual de reportes
            int current_reports = reports[index_reported].report_count;

            // verificar la cantidad de reportes
            if (current_reports > 10) {
                // bloquear al usuario
                kick_user(reported_pid);
                
                // dejar 'vacio' ese espacio
                if (index_reported < reports_count - 1) {
                    reports[index_reported].pid = 0;
                    reports[index_reported].report_count = 0;
                }
                reports_count--;

                printf("Usuario %d kickeado\n", reported_pid);

                // enviar confirmación de eliminación
                char report_msg[100];
                snprintf(report_msg, sizeof(report_msg), "Usuario %d kickeado por %d reportes\n", reported_pid, current_reports);
                write(fd_out, report_msg, strlen(report_msg));
            } else {
                // enviar reporte normal a la central
                char report_msg[100];
                snprintf(report_msg, sizeof(report_msg), "Usuario %d reportado, %d reportes\n", reported_pid, current_reports);
                write(fd_out, report_msg, strlen(report_msg));
            }
        }
        else if (n == 0) {
            printf("Central se desconectó\n");
            break;
        }
    }

    free(reports);
    close(fd_in);
    close(fd_out);
    return 0;
}