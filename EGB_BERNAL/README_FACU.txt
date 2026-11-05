COMANDOS EXAMEN:

--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Para conectarse a la RPI4:
		ssh lse@lse-pi4-04.local

		password: lse	


Comando para subir carpeta: sube archivos de la pc a la RPI, queda en el home de LSE, por eso el ":"  HACERLO DESDE AFUERA, DIRECTAMENTE EN CMD

		scp -r "C:\Users\camig\Desktop\Facu\EGB_BERNAL" lse@lse-pi4-04.local:

Comandos a ejecutar para examen raspberry: 

		dtc -@ -I dts -O dtb -o esp32-link.dtbo esp32-link-pi4-overlay.dts

				//compila el dts (texto) a dto (binario)

		sudo dtoverlay -d . esp32-link		//Aplica el overlay "en caliente"		

Para verificar: Verifica que el nodo quedó en el árbol en vivo. Si no devuelve nada, el overlay no se aplicó.

	find /sys/firmware/devicetree/base -name esp32-link


Para ejecutar:

	make		//compila el módulo de kernel

	sudo insmod build/esp32_link.ko //inserta el módulo

Verificar nodo:

	ls -l /dev/esp32link	//Verifica que el dispositivo existe. Tiene que empezar con c (carácter). 
				//Si empieza con -, es un archivo común y hay que borrarlo con sudo rm -f /dev/esp32link y recargar el módulo.

	gcc -Wall -pthread -o pid_ctl PID_CTL.c	//Compila el programa en C: 
						//-Wall muestra todos los warnings, 
						//-pthread enlaza la librería de hilos (usa un himdlo monitor) y 
						//-o eq_ctl nombra el ejecutable.

	sudo ./PID_CTL.c	//Lo ejecuta


Para borrar carpeta 
		rm -r EGB_BERNAL/




--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------



esp32-link-pi4-overlay.dts			CREO QUE YA NO HAY QUE MODIFICAR NADA	
makefile 					CREO QUE YA NO HAY QUE MODIFICAR NADA

esp32_link.c					CREO QUE YA NO HAY QUE MODIFICAR NADA
esp32_link.h					CREO QUE YA NO HAY QUE MODIFICAR NADA

PID_CTL						CREO QUE YA NO HAY QUE MODIFICAR NADA





--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	


