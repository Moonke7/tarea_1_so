#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>

volatile sig_atomic_t is_active = 1; // 1 = este proceso captura teclado

void sig_activate(int s) { is_active = 1; }
void sig_deactivate(int s) { is_active = 0; }

int open_write_retry(const char *path) {
    int fd;
    while (1) {
        fd = open(path, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) break;
        if (errno == ENXIO || errno == ENOENT) { // no hay lector todavía o fifo no existe
            usleep(100000); // 100ms
            continue;
        } else {
            perror("open_write_retry");
            return -1;
        }
    }
    // quitar O_NONBLOCK para escritura normal (opcional)
    int flags = fcntl(fd, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
    return fd;
}

int main() {
    pid_t main_pid = getpid();         // pid del proceso que arranca (padre original)
    pid_t child_pid = -1;              // pid del clone (si existe)

    // instalar handlers ANTES del fork para que ambos hereden las handlers
    struct sigaction sa1;
    sa1.sa_handler = sig_activate;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0;
    sigaction(SIGUSR1, &sa1, NULL);

    struct sigaction sa2;
    sa2.sa_handler = sig_deactivate;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, NULL);

    // nombres de pipes del proceso actual (por PID inicial)
    char my_out[64], my_in[64];
    snprintf(my_out, sizeof(my_out), "user_%d_out", (int)main_pid);
    snprintf(my_in,  sizeof(my_in),  "user_%d_in",  (int)main_pid);

    // crear fifos (ignorar si ya existen)
    if (mkfifo(my_out, 0666) < 0 && errno != EEXIST) { perror("mkfifo out"); }
    if (mkfifo(my_in,  0666) < 0 && errno != EEXIST) { perror("mkfifo in");  }

    // abrir extremos: escritura (hacia central) y lectura (desde central)
    int fd_out = open_write_retry(my_out); // bloquea/reintenta hasta que central abra lector
    if (fd_out < 0) { fprintf(stderr,"No pude abrir write pipe\n"); exit(1); }

    int fd_in = open(my_in, O_RDONLY | O_NONBLOCK); // lectura no bloqueante
    if (fd_in < 0) { perror("open fd_in"); /* seguir igual, se reintentará si hace falta */ }

    printf("Usuario conectado (PID: %d). /clone /useParent /useChild /report <pid> /exit\n", (int)main_pid);

    char line[512];
    char outmsg[1024];

    // variables para select
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;
        if (fd_in >= 0) { FD_SET(fd_in, &rfds); if (fd_in > maxfd) maxfd = fd_in; }
        if (is_active) { FD_SET(STDIN_FILENO, &rfds); if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO; }

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // lectura desde central (mensajes entrantes)
        if (fd_in >= 0 && FD_ISSET(fd_in, &rfds)) {
            char buf[512];
            int n = read(fd_in, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                printf("%s", buf); fflush(stdout);
            } else if (n == 0) {
                // EOF: central o extremo escritor cerró
                // cerramos fd_in y lo dejamos para reintento
                close(fd_in);
                fd_in = -1;
                printf("Central cerró pipe (fd_in). Esperando reconexión...\n");
            }
        }

        // lectura desde teclado SOLO si este proceso está activo
        if (is_active && FD_ISSET(STDIN_FILENO, &rfds)) {
            if (fgets(line, sizeof(line), stdin) == NULL) {
                // EOF del terminal
                break;
            }
            // eliminar newline
            line[strcspn(line, "\n")] = '\0';

            // COMANDO: /exit
            if (strcmp(line, "/exit") == 0) {
                pid_t me = getpid();
                if (me == main_pid) {
                    // si soy padre y tengo hijo, matarlo
                    if (child_pid > 0) {
                        kill(child_pid, SIGTERM);
                        usleep(100000);
                    }
                    // cerrar y eliminar mis pipes
                    close(fd_out);
                    if (fd_in >= 0) close(fd_in);
                    unlink(my_out);
                    unlink(my_in);
                    printf("Padre (%d) saliendo. Pipes eliminados.\n", (int)me);
                    exit(0);
                } else {
                    // soy hijo
                    // construir mis nombres según mi pid
                    pid_t mepid = getpid();
                    char outn[64], inn[64];
                    snprintf(outn, sizeof(outn), "user_%d_out", (int)mepid);
                    snprintf(inn,  sizeof(inn),  "user_%d_in",  (int)mepid);
                    close(fd_out);
                    if (fd_in >= 0) close(fd_in);
                    unlink(outn);
                    unlink(inn);
                    printf("Hijo (%d) saliendo. Pipes eliminados.\n", (int)mepid);
                    exit(0);
                }
            }

            // COMANDO: /clone  (solo el padre debe crear el hijo)
            if (strcmp(line, "/clone") == 0) {
                if (getpid() != main_pid) {
                    printf("Solo el proceso padre puede crear clones.\n");
                } else if (child_pid > 0) {
                    printf("Ya existe un hijo (PID=%d).\n", (int)child_pid);
                } else {
                    pid_t c = fork();
                    if (c < 0) {
                        perror("fork");
                    } else if (c == 0) {
                        // --- CODIGO DEL HIJO CLON ---
                        pid_t mypid = getpid();
                        // instalar handlers (ya heredados, pero para estar seguro)
                        signal(SIGUSR1, sig_activate);
                        signal(SIGUSR2, sig_deactivate);

                        // crear sus propios pipe names
                        char child_out[64], child_in[64];
                        snprintf(child_out, sizeof(child_out), "user_%d_out", (int)mypid);
                        snprintf(child_in,  sizeof(child_in),  "user_%d_in",  (int)mypid);

                        if (mkfifo(child_out, 0666) < 0 && errno != EEXIST) perror("mkfifo child_out");
                        if (mkfifo(child_in,  0666) < 0 && errno != EEXIST) perror("mkfifo child_in");

                        int cfd_out = open_write_retry(child_out);
                        if (cfd_out < 0) { fprintf(stderr,"Hijo: no pude abrir my out\n"); exit(1); }

                        int cfd_in = open(child_in, O_RDONLY | O_NONBLOCK);
                        if (cfd_in < 0) { perror("Hijo open cfd_in"); /* seguir */ }

                        // hijo arranca inactivo (no captura teclado)
                        is_active = 0;
                        printf("Clone creado (hijo PID=%d). Para activar usa /useChild desde padre o escribe /useParent desde aquí.\n", (int)mypid);

                        // reasignar fd_in/fd_out locales del hijo y continuar el loop normal
                        fd_in = cfd_in;
                        fd_out = cfd_out;
                        // IMPORTANTE: child_pid variable en hijo no es usada
                        child_pid = -1;
                        // ya dentro del mismo while; el hijo continuará el mismo loop pero con sus fds
                        continue;
                    } else {
                        // padre
                        child_pid = c;
                        printf("Clone creado (PID hijo: %d)\n", (int)child_pid);
                    }
                }
                continue;
            }

            // COMANDO: /useParent
            if (strcmp(line, "/useParent") == 0) {
                pid_t me = getpid();
                if (me == main_pid) {
                    // si soy padre: activo yo y desactivo hijo (si existe)
                    is_active = 1;
                    if (child_pid > 0) kill(child_pid, SIGUSR2);
                    printf(">> Ahora escribe el PADRE (%d)\n", (int)me);
                } else {
                    // soy hijo: aviso al padre y me desactivo
                    pid_t ppid = getppid();
                    kill(ppid, SIGUSR1);   // pedirle al padre que se active
                    raise(SIGUSR2);        // desactivar este proceso
                    printf(">> Petición enviada al PADRE (%d). Yo me desactivo.\n", (int)ppid);
                }
                continue;
            }

            // COMANDO: /useChild
            if (strcmp(line, "/useChild") == 0) {
                pid_t me = getpid();
                if (me == main_pid) {
                    if (child_pid > 0) {
                        // padre pide activar al hijo y se desactiva
                        kill(child_pid, SIGUSR1);
                        is_active = 0;
                        printf(">> Petición enviada al HIJO (%d). Padre se desactiva.\n", (int)child_pid);
                    } else {
                        printf("No hay hijo clonado.\n");
                    }
                } else {
                    // si soy hijo: activarme y desactivar padre
                    is_active = 1;
                    pid_t ppid = getppid();
                    kill(ppid, SIGUSR2);
                    printf(">> Ahora escribe el HIJO (%d)\n", (int)me);
                }
                continue;
            }

            // COMANDO: /report <pid>
            if (strncmp(line, "/report ", 8) == 0) {
                int reported;
                if (sscanf(line + 8, "%d", &reported) == 1) {
                    snprintf(outmsg, sizeof(outmsg), "%d:REPORT:%d\n", (int)getpid(), reported);
                    if (fd_out >= 0) write(fd_out, outmsg, strlen(outmsg));
                    printf("Report enviado para PID %d\n", reported);
                } else {
                    printf("Uso: /report <pid>\n");
                }
                continue;
            }

            // MENSAJE NORMAL -> enviar al central
            if (fd_out >= 0) {
                snprintf(outmsg, sizeof(outmsg), "%d:%s\n", (int)getpid(), line);
                write(fd_out, outmsg, strlen(outmsg));
            } else {
                printf("No puedo enviar: pipe de salida cerrado.\n");
            }
        } // fin handling stdin
    } // fin while

    return 0;
}