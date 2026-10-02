/*

Boton ORIGEN:   El motor va a 0°: el angulo deseado es 0°
Boton START:    ON/OFF del PID (con PID apagado, el motor no tiene fuerza). 
Boton STOP:     El motor FRENA en el angulo actual. El angulo deseado=angulo actual
Boton MODO:     Cambia los perfiles, entre RAMPA y ESCALON

En la terminal:
    set: angulo de 0 a 360. (enter).
    get: (enter). Me devuelve el angulo actual del eje.
    exec: ram (enter) o esc (enter). Cambio entre escalon y rampa. 

HOLA NAHHUIII 17:18
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "hd44780.h"
#include "as5600.h"
#include "pid_ctrl.h"   

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------DEFINICIONES -----------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_FREQ 100000     
#define LCD_DIR 0x27
#define SDA_PIN 5
#define SCL_PIN 4
#define I2C_CHN 0

#define PIN_ORIGEN 39       //DEFINES de botones
#define PIN_START 40        //DEFINES de botones
#define PIN_STOP 41         //DEFINES de botones
#define PIN_MODO 42         //DEFINES de botones

#define L298N_IN1   3       //PINES Puente H  
#define L298N_IN2   8       //PINES Puente H   
#define L298N_ENA   18       //PINES Puente H  

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT   // 0-1023
#define LEDC_FREQUENCY  100             
#define LEDC_DUTY_MAX   1023.0f
#define LEDC_DUTY_OBS   300.0f      //Para la implementación de detección de obstaculos     

#define MOTOR_MIN_DUTY  80.0f//85
#define BANDA_ERROR     2.0f
#define PID_time        10                        

#define STACK_SIZE_LED_blink 2048*2
#define STACK_SIZE_LCD_controller 2048*2
#define STACK_SIZE_encoder_controller 2048*2
#define STACK_SIZE_PID 2048*2
#define STACK_SIZE_UART 2048*2

#define UART_PORT      UART_NUM_1
#define TXD_PIN        (GPIO_NUM_45)
#define RXD_PIN        (GPIO_NUM_0)
#define BUF_SIZE       1024
#define UART_LINE_BUF_SIZE 64

#define RESOLUCION_RAMPA 5  //Grados del paso de la rampa

#define NVS_NAMESPACE   "motor_cfg"
#define NVS_KEY_ANGULO  "angulo_des"

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------DECLARACION DE VARIABLES------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

char *ESP_LOGI_TAG = "ESP-FB";      //TAG del ESP-LOGI

gpio_config_t BOTONERA_io_conf;
gpio_config_t L298N_io_conf;

static lcd_bus_hd44780_t *LCD1_BUS = NULL;
static pid_ctrl_block_handle_f_t pid_ctrl = NULL;
static as5600_handle_t as5600_dev = NULL;

static uint8_t ucParameterToPass;

pid_ctrl_parameter_f_t pid_params;

pid_ctrl_config_f_t pid_config;

hd44780_t *LCD1;

TaskHandle_t xHandle_LED=NULL;
TaskHandle_t xHandle_LCD=NULL;
TaskHandle_t xHandle_encoder=NULL;
TaskHandle_t xHandle_PID=NULL;
TaskHandle_t xHandle_UART=NULL;
TaskHandle_t xHandle_flash=NULL;

TaskHandle_t xHandle_BTN_ORIGEN=NULL;
TaskHandle_t xHandle_BTN_START=NULL;
TaskHandle_t xHandle_BTN_STOP=NULL;
TaskHandle_t xHandle_BTN_MODO=NULL;

QueueHandle_t queue_uart_pid;
QueueHandle_t queue_pid_uart;
QueueHandle_t queue_pid_lcd;
QueueHandle_t queue_pid_flash;

bool activo=0;  //Indica si el PID se encuentra activo o no
volatile bool rebote=0; //Implementación de antirrebote
bool lectura_flash=0;
bool ISR_on=0;

bool origen=0;      //VARIABLES DE BOTONES
bool start=0;       //VARIABLES DE BOTONES
bool stop=0;        //VARIABLES DE BOTONES
bool modo=0;        //VARIABLES DE BOTONES   0:escalon 1:rampa

volatile int OBS_detect=0;
volatile bool OBS_flag=0;
volatile int OBS_dir = 0;

float angulo_actual;
float angulo_deseado;
float PID_output=0.0f;

float PID_KP=0.55f;       //CONSTANTE PID
float PID_KI=0.00f;       //CONSTANTE PID
float PID_KD=0.14f;       //CONSTANTE PID

typedef struct {
    float angulo_actual_uart;
    float angulo_deseado_uart;
    int perfil_uart;       
    int estado_uart;  
} uart_data;

typedef struct {
    float angulo_actual_lcd;
    float angulo_deseado_lcd;
    int perfil_lcd;       
    int estado_lcd;  
} lcd_data;

typedef struct {
    float angulo_actual_pid;
    float angulo_deseado_pid;
    int perfil_pid;       
    int estado_pid;  
} pid_data;

float angulo_deseado_flash;


//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------DECLARACION DE FUNCIONES------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void GPIO_init();
void LCD_init();
void AS5600_init();
void PID_init ();
void PWM_init ();
void UART_init();
void flash_init ();
void ISR_init ();

void create_task();
void create_queue();

void motor_stop ();
float normalizar_error(float error);
void pid_escalon ();
void pid_rampa();
void lectura_datos_uart(const char *buffer, pid_data *datos);
void pid_actualizar (float PID_KP, float PID_KI,float PID_KD);
float leer_flash();

void task_I2C_guard ();

void isr_BTN_ORIGEN(void *arg);             //INTERRUPCIONES DE BOTONES
void task_BTN_ORIGEN (void *pvParameters);
void isr_BTN_START(void *arg);              //INTERRUPCIONES DE BOTONES
void task_BTN_START (void *pvParameters);
void isr_BTN_STOP(void *arg);               //INTERRUPCIONES DE BOTONES
void task_BTN_STOP (void *pvParameters);
void isr_BTN_MODO(void *arg);               //INTERRUPCIONES DE BOTONES
void task_BTN_MODO (void *pvParameters);

void task_H_controller(float pid_output);
void task_current_sens ();

void task_LCD_controller(void *pvParameters);
void task_encoder_controller(void *pvParameters);
void task_PID(void *pvParameters);
void task_UART(void *pvParameters);
void task_flash(void *pvParameters);

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------MAIN----------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void app_main(void)
{
    ISR_on=0;

    flash_init ();
    angulo_deseado=leer_flash();
    GPIO_init();
    LCD_init();     //LLAMAR PRIMERO A LCD_init antes que a AS5600_init
    AS5600_init ();
    PID_init();
    PWM_init();
    UART_init();
    
    create_queue();
    create_task();
     
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------INICIALIZACIONES------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void GPIO_init() //Inicialización de GPIO
{
    //CONFIGURACION GPIO DE LOS PINES DE BOTONERA
    BOTONERA_io_conf.pin_bit_mask = (1ULL << PIN_ORIGEN | 1ULL << PIN_START | 1ULL << PIN_STOP | 1ULL << PIN_MODO),
    BOTONERA_io_conf.mode = GPIO_MODE_INPUT,
    BOTONERA_io_conf.pull_up_en = GPIO_PULLUP_ENABLE,
    BOTONERA_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE,
    BOTONERA_io_conf.intr_type = GPIO_INTR_NEGEDGE,

    //CONFIGURACIÓN GPIO DE LOS PINES DE L298N
    L298N_io_conf.pin_bit_mask = (1ULL << L298N_IN1) | (1ULL << L298N_IN2),
    L298N_io_conf.mode = GPIO_MODE_OUTPUT,
    L298N_io_conf.pull_up_en = GPIO_PULLUP_DISABLE,
    L298N_io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE,
    L298N_io_conf.intr_type = GPIO_INTR_DISABLE,

    gpio_config(&BOTONERA_io_conf);
    gpio_config(&L298N_io_conf);

    gpio_set_level(L298N_IN1, 1);
    gpio_set_level(L298N_IN2, 0);

    gpio_install_isr_service(0);    //Instalar ISR service

    gpio_isr_handler_add(PIN_ORIGEN,    isr_BTN_ORIGEN, NULL); 
    gpio_isr_handler_add(PIN_START,     isr_BTN_START,  NULL);
    gpio_isr_handler_add(PIN_STOP,      isr_BTN_STOP,   NULL); 
    gpio_isr_handler_add(PIN_MODO,      isr_BTN_MODO,   NULL);    

    gpio_config(&BOTONERA_io_conf);
    gpio_config(&L298N_io_conf);


    return;
}

void LCD_init() //Inicialización del LCD E INICIALIZA EL I2C
{
    LCD1_BUS = lcd_bus_pcf8574_i2c_create( 
        I2C_CHN,
        LCD_DIR,
        SDA_PIN,
        SCL_PIN,
        I2C_FREQ);

    if (!LCD1_BUS) return;

    LCD1=lcd_init(LCD1_BUS, HD44780_GEOMETRY_20X4, true);
    if (!LCD1)
    {
        if (LCD1_BUS->destroy) LCD1_BUS->destroy(&LCD1_BUS);
        return;
    }

    lcd_backlight_on(LCD1);
    lcd_clear_screen(LCD1);

    lcd_set_cursor(LCD1, 0, 0);
    lcd_write_str(LCD1, " Tecnicas Digitales ");

    lcd_set_cursor(LCD1, 0, 1);
    lcd_write_str(LCD1, "        2026        ");

    lcd_set_cursor(LCD1, 0, 2);
    lcd_write_str(LCD1, "  Bernal -- Faraco  ");

    lcd_set_cursor(LCD1, 0, 3);
    lcd_write_str(LCD1, "    Control  PID    ");

    vTaskDelay (pdMS_TO_TICKS(2000));

    lcd_clear_screen(LCD1);

    lcd_set_cursor(LCD1, 0, 0);
    lcd_write_str(LCD1, "Actual : ");   //tengo que escribirlo en 10,0

    lcd_set_cursor(LCD1, 0, 1);
    lcd_write_str(LCD1, "Deseado: ");   //tengo que escribirlo en 10,1

    lcd_set_cursor(LCD1, 0, 2);
    lcd_write_str(LCD1, "Estado : ");   //tengo que escribirlo en 10,2

    lcd_set_cursor(LCD1, 0, 3);
    lcd_write_str(LCD1, "Perfil : ");    //tengo que escribirlo en 10,3

}

void AS5600_init()  //Inicialización del encoder AS5600
{
    i2c_master_bus_handle_t bus_handle = lcd_bus_pcf8574_get_i2c_bus(I2C_CHN);  //Toma el BUS de I2C generado por la función de LCD. Lo guarda en bus_handle

    if (bus_handle == NULL) {   //Si el bus obtenido no es correcto:
        ESP_LOGE(ESP_LOGI_TAG, "ERROR DE OBTENCIÓN BUS I2C");
        return;
    }

    as5600_i2c_config_t as5600_config = { .scl_speed_hz = I2C_FREQ };   //Configuración Clock de I2C

    esp_err_t rc = as5600_new_sensor(bus_handle, &as5600_config, &as5600_dev);  //Guarda en la variable rc lo que devuelve la creación del sensor

    if (rc != ESP_OK) {
        ESP_LOGE(ESP_LOGI_TAG, "Error creando sensor AS5600: %s", esp_err_to_name(rc));
        return;
    }

    ESP_LOGI(ESP_LOGI_TAG, "AS5600 inicializado correctamente");

    return;
}

void PWM_init (){   //Inicialización del PWM (mediante la librería LEDC)

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = L298N_ENA,
        .duty           = 0,
        .hpoint         = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    return;
}

void PID_init ()    //Inicialización del control PID. Se utiliza la librería pid_ctrl
{
        pid_params.kp = PID_KP,
        pid_params.ki = PID_KI,
        pid_params.kd = PID_KD,
        pid_params.max_output = LEDC_DUTY_MAX,
        pid_params.min_output = -LEDC_DUTY_MAX,
        pid_params.max_integral = LEDC_DUTY_MAX,   // anti-windup
        pid_params.min_integral = -LEDC_DUTY_MAX,  // anti-windup
        pid_params.cal_type = PID_CAL_TYPE_POSITIONAL,

    pid_config.init_param = pid_params;
 
    ESP_ERROR_CHECK(pid_new_control_block_f(    //Creo el control PID
        &pid_config,    //Configuración de PID
        &pid_ctrl));    //PID creado

    return; 
}

void UART_init(){     //Inicialización de UART
    uart_config_t uart_config = {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));    // Instalar driver con buffer de recepción
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    char InitMsg[] = "Ingrese el angulo deseado (entre 0 y 360) como: set xx.xx seguido de Enter.\r\n"
        "Ingrese el perfil deseado (Escalon o Rampa) como: exec esc/ram seguido de Enter.\r\n"
        "Para obtener el angulo actual, ingrese: get seguido de Enter.\r\n";
    uart_write_bytes(UART_PORT, InitMsg, strlen(InitMsg));

    return;
}

void flash_init (){ //Inicialización de flash
    esp_err_t ret=nvs_flash_init();

    if (ret==ESP_ERR_NVS_NO_FREE_PAGES||ret==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret=nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------CREACION-------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void create_task()  //Función para crear todas las tareas
{
    xTaskCreate(task_LCD_controller,    "task_LCD_controller",      STACK_SIZE_LCD_controller,      &ucParameterToPass,2,   &xHandle_LCD);          //TAREA LCD_controller
    xTaskCreate(task_encoder_controller,"task_encoder_controller",  STACK_SIZE_encoder_controller,  &ucParameterToPass,1,   &xHandle_encoder);      //TAREA encoder_controller
    xTaskCreate(task_PID,               "task_PID",                 STACK_SIZE_PID,                 &ucParameterToPass,1,   &xHandle_PID);          //TAREA task_PID
    xTaskCreate(task_UART,              "task_UART",                STACK_SIZE_UART,                &ucParameterToPass,3,   &xHandle_UART);         //TAREA UART
    xTaskCreate(task_flash,             "task_flash",               4096,                           &ucParameterToPass,3,   &xHandle_flash);        //TAREA flash
    xTaskCreate(task_BTN_ORIGEN,        "task_BTN_ORIGEN",          4096,                           &ucParameterToPass,2,   &xHandle_BTN_ORIGEN);   //TAREA BOTON ORIGEN
    xTaskCreate(task_BTN_START,         "task_BTN_START",           4096,                           &ucParameterToPass,2,   &xHandle_BTN_START);    //TAREA BOTON START
    xTaskCreate(task_BTN_STOP,          "task_BTN_STOP",            2048,                           &ucParameterToPass,2,   &xHandle_BTN_STOP);     //TAREA BOTON STOP
    xTaskCreate(task_BTN_MODO,          "task_BTN_MODO",            2048,                           &ucParameterToPass,2,   &xHandle_BTN_MODO);     //TAREA BOTON MODO

    return;
}

void create_queue ()    //TAREA QUE CREA LAS QUEUES
{   
    queue_uart_pid=xQueueCreate(10,sizeof(pid_data));
    queue_pid_lcd=xQueueCreate(5, sizeof(lcd_data));
    queue_pid_uart=xQueueCreate(5, sizeof(uart_data)); 
    queue_pid_flash=xQueueCreate(5, sizeof(float));  
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------TAREAS---------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void task_LCD_controller(void *pvParameters)    //TAREA CONTROLLADORA DEL LCD
{
    lcd_data datos;
    while (1)
    {
        if (xQueueReceive(queue_pid_lcd, &datos, portMAX_DELAY)==pdTRUE) {

            lcd_set_cursor(LCD1, 8, 0); //Angulo actual
            char buf0[32];
            snprintf(buf0, sizeof(buf0), "%.2f    ", datos.angulo_actual_lcd);
            lcd_write_str(LCD1, buf0);

            lcd_set_cursor(LCD1, 8, 1); //Angulo deseado
            char buf1[32];
            snprintf(buf1, sizeof(buf1), "%.2f    ", datos.angulo_deseado_lcd);
            lcd_write_str(LCD1, buf1);

            lcd_set_cursor(LCD1, 8, 2); //Estado
            char buf3[32];
            if (datos.estado_lcd==0)        snprintf(buf3, sizeof(buf3), "%s", "En lugar    ");
            else if (datos.estado_lcd==1)   snprintf(buf3, sizeof(buf3), "%s", "Calculando..");
            else if (datos.estado_lcd==2)   snprintf(buf3, sizeof(buf3), "%s", "Inactivo    ");
            else if (datos.estado_lcd==3)   snprintf(buf3, sizeof(buf3), "%s", "Bloqueado   ");
            lcd_write_str(LCD1, buf3);

            lcd_set_cursor(LCD1, 8, 3); //Perfil
            char buf2[32];
            if ((datos.perfil_lcd)==0) snprintf(buf2, sizeof(buf2), "%s", "ESCALON");
            else snprintf(buf2, sizeof(buf2), "%s", "RAMPA  ");
            lcd_write_str(LCD1, buf2);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_PID (void *pvParameters)  //CALCULA el PID que se debe aplicar. Muestra los parámetros por consola y por el LCD
{
    while (1) {
        pid_data datos_uart;

        if (xQueueReceive(queue_uart_pid, &datos_uart, 0) == pdTRUE) {
            angulo_deseado = datos_uart.angulo_deseado_pid;
            xQueueSend(queue_pid_flash, &angulo_deseado, 0);
            modo = datos_uart.perfil_pid;
        }

        if (!activo){
            lcd_data datos;
            datos.angulo_actual_lcd=angulo_actual;
            datos.angulo_deseado_lcd=angulo_deseado;
            datos.perfil_lcd= modo;  
            datos.estado_lcd=2; //ESTADO INACTIVO
            xQueueSend(queue_pid_lcd, &datos, 0);
            motor_stop();
        } 
        else if (modo==1) pid_rampa();       
        else if(modo==0)pid_escalon();

        lcd_data datos;
            datos.angulo_actual_lcd=angulo_actual;
            datos.angulo_deseado_lcd=angulo_deseado;
            datos.perfil_lcd= modo;  
            datos.estado_lcd=1; //ESTADO CALCULANDO. . . .
        xQueueSend(queue_pid_lcd, &datos, 0);

        vTaskDelay(pdMS_TO_TICKS(PID_time));

        ISR_on=1;
    }
}

void task_encoder_controller(void *pvParameters)    //TAREA CONTROLADORA DEL ENCODER VACIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
{
    while (1){
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_H_controller(float pid_output) //Aplica la respuesta PID necesaria al motor
{
    float magnitud = fabsf(pid_output); //Calcula valor absoluto de la salida generada por el PID
    
    static float ang_prev=0.0f;

    if (!activo) pid_output=0.0;        //Si se desactivó  el PID. . . 

    if (magnitud<MOTOR_MIN_DUTY && magnitud > 0.0f)  magnitud=MOTOR_MIN_DUTY;   //Si la salida es menor que el MIN_DUTY...

    if (magnitud>=LEDC_DUTY_MAX)  magnitud = LEDC_DUTY_MAX;     //Si la salida es mayor que el MAX_DUTY...
       

    if (magnitud>0.0f && fabsf(angulo_actual-ang_prev)<2.0f) OBS_detect++;   //Si esta frenado en un  angulo, y se quiere mover, detect++
    else OBS_detect=0;

    ang_prev=angulo_actual;

    if (OBS_detect>50){
        //OBS_flag=1;
        OBS_dir=(pid_output>0.0f) ? -1 : 1;   // el sentido contrario al que se trabó
        OBS_detect = 0;
    }


    if (pid_output > 0.0f) {        //Si la salida es positiva... HORARIO
        gpio_set_level(L298N_IN1, 1);
        gpio_set_level(L298N_IN2, 0);
    } 
    else if (pid_output < 0.0f) {   //Si la salida es negativa... ANTIHORARIO
        gpio_set_level(L298N_IN1, 0);
        gpio_set_level(L298N_IN2, 1);
    } 
    else {                          //Si la salida es CERO...
        gpio_set_level(L298N_IN1, 0);
        gpio_set_level(L298N_IN2, 0);   
    }

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (uint32_t)magnitud); //Coloca el PWM necesario
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void task_current_sens ()
{
    if(OBS_detect>50) OBS_flag=1;
}

void task_UART(void *pvParameters)
{
    uint8_t data[BUF_SIZE];
    char buffer[UART_LINE_BUF_SIZE]; //buffer para almacenar la linea completa de entrada
    int pos = 0; //posicion actual en el buffer de linea

    pid_data datos;
    
    while (1) {

        int len=uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100)); //lee el angulo ingresado
 
        if (len>0) { //cuando la funcion uart_read_bytes() devuelve una cantidad de bytes comienza:
            
            for (int i=0; i<len; i++) { //recorre cada caracter recibido
                char c=(char)data[i]; 
                if (c=='\n'||c=='\r') {     //cuando llega al final...
                    if (pos>0) { //si habia caracteres acumulados, se agrega /0 para q sea string
                        buffer[pos]='\0';                    
                        lectura_datos_uart (buffer, &datos);
                        pos = 0; //reinicia la posicion del buffer de linea para la proxima entrada
                    }
                    xQueueSend(queue_uart_pid, &datos, 0);
                } else if (pos<(UART_LINE_BUF_SIZE-1))buffer[pos++]=c; //si el caracter recibido no es salto y buffer no esta lleno, se agrega el caracter al buffer                    
            }
        }
    }   
}

void task_flash(void *pvParameters){
    float data_recibido;

    while(1){
        if(xQueueReceive(queue_pid_flash, &data_recibido, portMAX_DELAY)==pdTRUE){  //Se recibieron datos para guardar...
            lectura_flash=1;
            nvs_handle_t handle;
            esp_err_t err=nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

            if (err!=ESP_OK) return;
    
            err=nvs_set_blob(handle, NVS_KEY_ANGULO, &data_recibido, sizeof(float));//Guarda en ángulo con el key 

            if (err==ESP_OK) err=nvs_commit(handle);
    
            if (err!=ESP_OK)  ESP_LOGW("FLASH", "Error guardando angulo en NVS: %s", esp_err_to_name(err));

            nvs_close(handle);
            ESP_LOGE("FLASH", "Angulo deseado guardado en NVS: %.2f", data_recibido);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------FUNCIONES------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void motor_stop ()  //FRENA el motor
{
    lcd_data datos;
        datos.angulo_actual_lcd=angulo_actual;
        datos.angulo_deseado_lcd=angulo_deseado;
        datos.perfil_lcd= modo;  
        datos.estado_lcd=0; //ESTADO DETENIDO
        xQueueSend(queue_pid_lcd, &datos, 0);

    gpio_set_level(L298N_IN1, 0);
    gpio_set_level(L298N_IN2, 0);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    OBS_flag=0;
    return;
}

float normalizar_error(float error)
{
    while(error>180.0f) error-=360.0f;
    while(error<-180.0f) error+=360.0f;

    if(OBS_flag){
        if(fabsf(error)<20.0f){
            OBS_flag=0;
        }else{
            if(OBS_dir>0&&error<0.0f) error+=360.0f;
            if(OBS_dir<0&&error>0.0f) error-=360.0f;
        }
    }
    return error;
}

void lectura_datos_uart(const char *buffer, pid_data *datos){
    
    bool pid_mod=0;

    if (strncmp(buffer,"set ", 4)==0){   //Si empieza con set, Se intentó ingresar un angulo
        char *endptr = NULL;
        float angulo_recibido = strtof(buffer+4, &endptr);      //Para saltear el set 

        if (endptr==buffer) {                                   //EL VALOR ES INVALIDO
            const char *err = "ERR: dato invalido\r\n";
            uart_write_bytes(UART_PORT, err, strlen(err));

        } else if (angulo_recibido<0 ||angulo_recibido>360){    //EL VALOR ESTÁ FUERA DE RANGO
            const char *err = "ERR: fuera de rango\r\n";
            uart_write_bytes(UART_PORT, err, strlen(err));

        } else{  //EL ANGULO ES VALIDO  
            datos->angulo_deseado_pid=angulo_recibido;        
            xQueueSend(queue_pid_flash,&angulo_deseado,0);
        } 
    }else if (strncmp(buffer,"get",3)==0){  //Si se usa get
        char out[64];
        snprintf(out, sizeof(out), "Angulo actual: %.2f\r\n", angulo_actual);
        uart_write_bytes(UART_PORT, out, strlen(out));

    }else if(strcmp(buffer,"exec esc")==0) datos->perfil_pid=0;   //Caso de cambio de perfil A ESCALON
    else if (strcmp(buffer,"exec ram")==0) datos->perfil_pid=1;   //Caso de cambio de perfil A RAMPA
    else if (strncmp(buffer, "KP=", 3)==0) {    //Modificiación parametros PID
        float nuevo_kp = strtof(buffer+3, NULL);
        PID_KP=nuevo_kp;
        pid_mod=1;
    } else if (strncmp(buffer, "KI=", 3)==0) {
        float nuevo_ki = strtof(buffer + 3, NULL);
        PID_KI = nuevo_ki;
        pid_mod=1;
    } else if (strncmp(buffer, "KD=", 3)==0) {
        float nuevo_kd = strtof(buffer + 3, NULL);
        PID_KD = nuevo_kd;
        pid_mod=1;
    }    

    if(pid_mod) pid_actualizar (PID_KP, PID_KI, PID_KD);    ///Si se modificó el PID

    return;
}

void pid_actualizar (float PID_KP, float PID_KI,float PID_KD){
    pid_params.kp = PID_KP;     
    pid_params.ki = PID_KI;
    pid_params.kd = PID_KD;

    pid_update_parameters(pid_ctrl, &pid_params);

    return;
}

void pid_escalon(){

    pid_actualizar (PID_KP, PID_KI, PID_KD);

    esp_err_t err = as5600_get_angle_degrees(as5600_dev, &angulo_actual);    //Lee angulo del encoder

    if (err != ESP_OK) ESP_LOGE(ESP_LOGI_TAG, "Error leyendo AS5600: %s", esp_err_to_name(err)); //Detecta si hay error en la lectura del ángulo

    float error=angulo_deseado-angulo_actual;   //Crea la variable de error

    error=normalizar_error(error);  //Normalizo el error para que vaya por el camino mas corto

    if (fabsf(error) < BANDA_ERROR) {  //Si el error está dentro del ángulo permitido. . . 
        motor_stop();   //Frena motor
        pid_reset_ctrl_block_f(pid_ctrl);   //Frena el control PID
    } else {
        ESP_ERROR_CHECK(pid_compute_f(pid_ctrl, error, &PID_output));   //Calculo la respuesta PID necesaria
        task_H_controller(PID_output);                                  //Aplico la respuesta PID al motor
    }

    ESP_LOGI(ESP_LOGI_TAG,"Angulo actual: %.2f | deseado: %.2f | PID: %.2f| ESCALON", angulo_actual, angulo_deseado, PID_output);   //Muestro valores relevantes
    return;
}

void pid_rampa(){

    pid_actualizar (15.0f, 0.0f, 0.1f);

    esp_err_t err = as5600_get_angle_degrees(as5600_dev, &angulo_actual);    //Lee angulo del encoder
    if (err != ESP_OK) ESP_LOGE(ESP_LOGI_TAG, "Error leyendo AS5600: %s", esp_err_to_name(err)); //Detecta si hay error en la lectura del ángulo

    float error=normalizar_error(angulo_deseado-angulo_actual); //Normalizo el error
    int signo_error=(error>0.0f)?1:(error<0.0f?-1:0);       //Obtengo el signo del error

    float paso = fminf(RESOLUCION_RAMPA, fabsf(error));     //Para que la rampa tenga un paso no constante
    float angulo_deseado_rampa=angulo_actual+signo_error*paso;

    float error_rampa=angulo_deseado_rampa-angulo_actual;

    if (fabsf(error) < BANDA_ERROR) {
        motor_stop();
        pid_reset_ctrl_block_f(pid_ctrl);
    } else {
        ESP_ERROR_CHECK(pid_compute_f(pid_ctrl, error_rampa, &PID_output));
        task_H_controller(PID_output);
    }

    ESP_LOGI(ESP_LOGI_TAG,
        "Act: %.2f | Dest: %.2f | e: %.2f | e_rampa: %.2f | PID: %.2f | RAMPA",
        angulo_actual, angulo_deseado, error, error_rampa, PID_output);
    return;
}

float leer_flash()    //Función para TOMAR el ángulo deseado de FLASH
{
    nvs_handle_t handle;
    float angulo=0.0f; // valor por defecto si es la primera vez

    esp_err_t err=nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err!=ESP_OK) {
        ESP_LOGI("FLASH", "No hay config previa en NVS, usando default (%.2f)", angulo);
        return angulo;
    }
    
    size_t size=sizeof(angulo);
    err = nvs_get_blob(handle, NVS_KEY_ANGULO, &angulo, &size);

    if (err != ESP_OK) {
        ESP_LOGW("FLASH", "No se pudo leer angulo de NVS, usando default");
        angulo = 0.0f;

    } else ESP_LOGI("FLASH", "Angulo deseado recuperado de NVS: %.2f", angulo);
    
    nvs_close(handle);
    return angulo;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------INTERRUPCIONES DE BOTONES---------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------


void IRAM_ATTR isr_BTN_ORIGEN(void *arg)    //Interrupción de boton ORIGEN
{
    if (!rebote){
        rebote=1;
        xTaskResumeFromISR(xHandle_BTN_ORIGEN);
    }
    return;
}

void IRAM_ATTR isr_BTN_START(void *arg)     //Interrupción de boton START / PIDE UN ANGULO DESEADO
{   
    if (!rebote){
        rebote=1;
        xTaskResumeFromISR(xHandle_BTN_START);
    }
    return;
}

void IRAM_ATTR isr_BTN_STOP(void *arg)      //Interrupción de boton STOP/ calibrar CERO
{
    if (!rebote){
        rebote=1;
        xTaskResumeFromISR(xHandle_BTN_STOP);
    }
    return;
}

void IRAM_ATTR isr_BTN_MODO(void *arg)      //Interrupción de boton MODO 
{
    if (!rebote){
        rebote=1;
        xTaskResumeFromISR(xHandle_BTN_MODO);
    }
    return;
}

void task_BTN_ORIGEN (void *pvParameters)  //Función del boton ORIGEN
{
    while (1){
        angulo_deseado=0.0;
        vTaskDelay(pdMS_TO_TICKS(1000));
        rebote=0;
        vTaskSuspend(NULL);
    }
}

void task_BTN_START (void *pvParameters)  //Función del boton START:
{
    while (1){
        activo=!activo;
        vTaskDelay(pdMS_TO_TICKS(1000)); //para antirrebote
        rebote=0;
        vTaskSuspend(NULL);
    }
}

void task_BTN_STOP (void *pvParameters)  //Cambia el angulo deseado a la posición actual del eje
{
    while (1){
        as5600_get_angle_degrees(as5600_dev, &angulo_deseado);
        xQueueSend(queue_pid_flash,&angulo_deseado,0);
        vTaskDelay(pdMS_TO_TICKS(1000));
        rebote=0;
        vTaskSuspend(NULL);
    }
}

void task_BTN_MODO (void *pvParameters)  //Función del boton MODO: Modifica perfiles mediante los coeficientes PID
{
    while (1){
        modo=!modo;
        vTaskDelay(pdMS_TO_TICKS(1000));
        rebote=0;
        vTaskSuspend(NULL);
    }
}