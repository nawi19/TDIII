import os, sys

DEV_PATH = "/dev/p1_led_btn" # Definimos ubicacion del archivo

def main():
    led_on = False

    while True:
        os.system("clear") # Limpiamos la consola
        print("--------------------------------------------------------")
        print("-------------------{ Aplicación CLI }-------------------")
        print("--------------------------------------------------------")
        print("Ingrese accion a realizar:")
        print("--------------------------------------------------------")
        print("1> Toggle LED")
        print("2> Consultar botón")
        print("3> Salir")
        print("--------------------------------------------------------")
        
        match input("Opción [1-3]: ").strip():
            case "1":
                nuevo_estado = "0" if led_on else "1"

                try:
                    with open(DEV_PATH, "w") as dev:
                        dev.write(nuevo_estado)

                    led_on = not led_on
                    estado_str = "encendido" if led_on else "apagado"
                    print("--------------------------------------------------------")
                    print(f"LED {estado_str}")
                    print("--------------------------------------------------------")
                except Exception:
                    print(f"\nError al escribir en {DEV_PATH}")
                 
            case "2":
                try:
                    with open(DEV_PATH, "r") as dev:
                        resp = dev.read().strip()
                    print(resp)
                    print("--------------------------------------------------------")
                    if resp == "1":
                        print("Botón presionado")
                    elif resp == "0":
                        print("Botón no presionado")
                    print("--------------------------------------------------------")
                except Exception:
                    print(f"\nError al leer de {DEV_PATH}")

            case "3":
                os.system("clear")
                break

            case _:
                print("\nOpción invalida")

        input("Presione tecla para continuar...")

if __name__ == "__main__":
    if not os.path.exists(DEV_PATH):
        print(f"Dispositivo {DEV_PATH} no encontrado.")
    else:
        main()
        