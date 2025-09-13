# Tarea 1 sistemas operativos

Para esta tarea se buscaba crear un sistema de chat grupal usando 3 procesos principales; Uno central que recibira todos los mensajes de todos los usuarios, un proceso de reportes que manejara la cantidad de reportes de cada usuario y un proceso de usuario, el cual puede clonarse a si mismo para generar mas usuarios dentro del sistema.
## Compilacion y ejecucion
Inicialmente, se debe clonar este repositorio y guardarlo dentro del directorio de su preferencia.

Luego, dentro del directorio se deben compilar y ejecutar los codigos correspondiente utilizando los siguientes codigos:.

Compilacion:
```sh
gcc central.c -o central
gcc reportes.c -o reportes
gcc user.c -o user
```

Ejecucion (en el mismo orden)
```sh
./reportes
./central
./user
```
## Funcionamiento

Como se dijo anteriormente, este código consta de 3 programas independientes, donde cada uno tiene su lógica y funcionamiento propio:

- **Central**  
  El proceso de central utiliza un arreglo de tamaño variable para almacenar la información de cada usuario, específicamente guarda su pid, su fifo de entrada, fifo de salida, su estado (1 = activo, 0 = inactivo) y dos arreglos de tipo char que guardarán el nombre del fifo que se creará.
  
  Luego se tendrán distintas funciones para cubrir tareas esenciales para la correcta ejecución del proceso:
    
    - **int add_user()**: Esta función es la encargada de agregar el nuevo usuario al arreglo de usuario, verificando que haya espacio suficiente y abriendo sus pipes en el proceso. Retorna 0 si se agregó correctamente y -1 si hubo algún error.

    - **void scan_for_new_pipes()**: Esta función es la encargada de detectar nuevos usuarios. Para esto, estará constantemente escuchando al directorio para ver si se crearon nuevos fifos. En caso de encontrar alguno (los cuales contienen el pid del proceso en su nombre) extraerá el pid de su proceso y llamará a la función add_user para añadir al nuevo usuario.

    - **void broadcast_message()**: Esta función cumple el rol de retransmitir a todos los usuarios cualquier mensaje que envíe un usuario, excepto al proceso que lo envió.

    - **void handle_report()**: Esta función se encarga de conectar a la central con el proceso de reportes. Su rol es enviar el pid reportado a dicho proceso.
    
    Finalmente, dentro del int main se inicializará el arreglo de usuarios y se abrirán los fifos hacia el proceso de reportes. Luego, se iniciará un ciclo while donde se llamará a la función para escanear nuevos pipes, la cual gracias al uso de un select, se ejecutará cada 1 segundo. Además, junto con el escaneo de nuevos fifos, también se estarán escuchando los mensajes que envíen todos los usuarios, donde en caso de que sean del tipo /report {pid}, hará uso de la función 'handle_report'; caso contrario, simplemente imprimirá el mensaje.

- **Reportes**  
  De manera similar al proceso anterior, se creará una estructura que guardará el pid de un proceso y un entero que representará la cantidad de reportes hechos a dicho proceso. El funcionamiento es simple: se tendrá un ciclo para escuchar constantemente los pid enviados por la central a través de los fifos que conectan ambos procesos. Cada pid enviado se agregará a un arreglo de tamaño dinámico creado previamente, el cual será del tipo de la estructura creada antes. De esta forma, se mantendrá un conteo de los reportes de cada pid y, en caso de que la cantidad de reportes de un proceso sea mayor a 10, se llamará a la siguiente función:
  - **void kick_user()**: Esta función recibirá un pid y matará el proceso que le pertenezca, cerrando y eliminando sus pipes en el proceso.

  Al eliminar un usuario, se enviará un mensaje de confirmación a la central.

- **Usuario**  
  Para el proceso de usuario se tiene que, inicialmente, se crean dos pipes con nombres únicos utilizando el pid del proceso, usando la forma:
  - user_{pid}_out  
  - user_{pid}_in  

  Para luego crear y abrir los fifos de dichos pipes. Luego, se hará un fork con el fin de tener un proceso hijo encargado únicamente de escuchar los mensajes que lleguen desde la central. Por otro lado, el proceso padre será el encargado de leer los mensajes de teclado y enviarlos hacia la central a través de fd_out (fifo de salida). Sin embargo, antes de enviar el mensaje a la central, verificará si el usuario escribió alguno de los siguientes comandos: 
| Comando        | Argumento   | Descripción                                                                                       |
|----------------|-------------|---------------------------------------------------------------------------------------------------|
| **/exit**      | Ninguno     | Cierra los fifos y los elimina del directorio, para luego terminar el proceso.                    |
| **/report**    | `{pid}`     | Recibe como argumento el pid del proceso a reportar y envía el reporte a la central en un formato fácil de identificar. |
| **/clone**     | Ninguno     | Hace un fork del proceso y lo ejecuta en una terminal aparte usando el comando `gnome`. Crea un usuario con las mismas funciones que el original, pero independiente, con su propio pid y terminal. |

  
  En caso de que no sea ninguno de estos comandos, simplemente se enviará el mensaje a la central, donde esta lo reenviará a los demás usuarios.
