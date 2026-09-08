/*

Boton ORIGEN:   El motor va a 0°: el angulo deseado es 0°
Boton START:    Empieza a funcionar el PID. Si se aprieta con el motor en determinada posición, lo toma como origen=0°
Boton STOP:     El motor FRENA en el angulo actual. El angulo deseado=angulo actual
Boton MODO:     Cambia los perfiles, cambiando los coeficientes PID

La librería del AS5600 esta mod
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
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
#include "driver/ledc.h"

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

#define PID_KP  5.5f        //CONSTANTE PID
#define PID_KI  0.3f        //CONSTANTE PID
#define PID_KD  12.0f        //CONSTANTE PID

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT   // 0-1023
#define LEDC_FREQUENCY  100             
#define LEDC_DUTY_MAX   1023.0f
#define LEDC_DUTY_OBS   600.0f      //Para la implementación de detección de obstaculos     

#define MOTOR_MIN_DUTY  100.0f
#define BANDA_ERROR     1.0f
#define PID_time        10                        

#define LED_1 11
#define LED_2 12

#define STACK_SIZE_LED_blink 2048*2
#define STACK_SIZE_LCD_controller 2048*2
#define STACK_SIZE_encoder_controller 2048*2
#define STACK_SIZE_PID 2048*2

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------DECLARACION DE VARIABLES------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

char *ESP_LOGI_TAG = "ESP-FB";      //TAG del ESP-LOGI

static lcd_bus_hd44780_t *LCD1_BUS = NULL;
static pid_ctrl_block_handle_f_t pid_ctrl = NULL;
static as5600_handle_t as5600_dev = NULL;

static uint8_t ucParameterToPass;

TaskHandle_t xHandle_LED=NULL;
TaskHandle_t xHandle_LCD=NULL;
TaskHandle_t xHandle_encoder=NULL;
TaskHandle_t xHandle_PID=NULL;

TaskHandle_t xHandle_BTN_ORIGEN=NULL;
TaskHandle_t xHandle_BTN_START=NULL;
TaskHandle_t xHandle_BTN_STOP=NULL;
TaskHandle_t xHandle_BTN_MODO=NULL;

QueueHandle_t queue_I2C_LCD;

bool led1=0;
bool led2=0;
bool activo=0;  //Indica si el PID se encuentra activo o no
volatile bool rebote=0; //Implementación de antirrebote

bool origen=0;      //VARIABLES DE BOTONES
bool start=0;       //VARIABLES DE BOTONES
bool stop=0;        //VARIABLES DE BOTONES
bool modo=0;        //VARIABLES DE BOTONES

int OBS_detect=0;
bool OBS_flag=0;

float angulo_actual = 0.0f;
float angulo_deseado = 0.0f;
float PID_output = 0.0f;

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------DECLARACION DE FUNCIONES------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void GPIO_init();
void LCD_init();
void AS5600_init();
void PID_init ();
void PWM_init ();

void LED_blink();
void create_task();
void create_queue();

void MOTOR_stop ();
float normalizar_error(float error);
void task_H_controller(float pid_output);
void task_current_sens ();

void task_I2C_guard ();

void isr_BTN_ORIGEN(void *arg);             //INTERRUPCIONES DE BOTONES
void task_BTN_ORIGEN (void *pvParameters);
void isr_BTN_START(void *arg);              //INTERRUPCIONES DE BOTONES
void task_BTN_START (void *pvParameters);
void isr_BTN_STOP(void *arg);               //INTERRUPCIONES DE BOTONES
void task_BTN_STOP (void *pvParameters);
void isr_BTN_MODO(void *arg);               //INTERRUPCIONES DE BOTONES
void task_BTN_MODO (void *pvParameters);

void vTaskl_LED_blink(void *pvParameters);
void task_LCD_controller(void *pvParameters);
void task_encoder_controller(void *pvParameters);
void task_PID(void *pvParameters);

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------MAIN--------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void app_main(void)
{
    GPIO_init();
    LCD_init();     //LLAMAR PRIMERO A LCD_init antes que a AS5600_init
    AS5600_init ();
    PID_init();
    PWM_init();

    create_queue();
    create_task();
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------INICIALIZACIONES------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void GPIO_init() //Inicialización de GPIO
{
    gpio_config_t BOTONERA_io_conf = {       //CONFIGURACION GPIO DE LOS PINES DE BOTONERA
        .pin_bit_mask = (1ULL << PIN_ORIGEN | 1ULL << PIN_START | 1ULL << PIN_STOP | 1ULL << PIN_MODO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

        gpio_config_t L298N_io_conf = {   //CONFIGURACIÓN GPIO DE LOS PINES DE L298N
        .pin_bit_mask = (1ULL << L298N_IN1) | (1ULL << L298N_IN2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
  
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


    gpio_reset_pin(LED_1);
    gpio_set_direction(LED_1, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED_2);
    gpio_set_direction(LED_2, GPIO_MODE_OUTPUT);

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

    hd44780_t *LCD1 = lcd_init(LCD1_BUS, HD44780_GEOMETRY_20X4, true);
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
    pid_ctrl_parameter_f_t pid_params = {   //Defino todos los parámetros del PID
        .kp = PID_KP,
        .ki = PID_KI,
        .kd = PID_KD,
        .max_output = LEDC_DUTY_MAX,
        .min_output = -LEDC_DUTY_MAX,
        .max_integral = LEDC_DUTY_MAX,   // anti-windup
        .min_integral = -LEDC_DUTY_MAX,  // anti-windup
        .cal_type = PID_CAL_TYPE_POSITIONAL,
    };
 
    pid_ctrl_config_f_t pid_config = { .init_param = pid_params,};  //Cargo los parámetros en la configuración.
 
    ESP_ERROR_CHECK(pid_new_control_block_f(    //Creo el control PID
        &pid_config,    //Configuración de PID
        &pid_ctrl));    //PID creado

    return; 
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------CREACION-------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void create_task()  //Función para crear todas las tareas
{
    xTaskCreate(        //TAREA LED BLINK
        vTaskl_LED_blink,
        "vTaskl_LED_blink",
        STACK_SIZE_LED_blink,
        &ucParameterToPass,
        1,
        &xHandle_LED);

     xTaskCreate(       //TAREA LCD_controller
        task_LCD_controller,
        "task_LCD_controller",
        STACK_SIZE_LCD_controller,
        &ucParameterToPass,
        2,
        &xHandle_LCD);

    xTaskCreate(        //TAREA encoder_controller
        task_encoder_controller,
        "task_encoder_controller",
        STACK_SIZE_encoder_controller,
        &ucParameterToPass,
        1,
        &xHandle_encoder);

    xTaskCreate(        //TAREA task_PID
        task_PID,
        "task_PID",
        STACK_SIZE_PID,
        &ucParameterToPass,
        1,
        &xHandle_PID);

    xTaskCreate(        //TAREA BOTON ORIGEN
        task_BTN_ORIGEN,
        "task_BTN_ORIGEN",
        4096,
        &ucParameterToPass,
        1,
        &xHandle_BTN_ORIGEN);

    xTaskCreate(        //TAREA BOTON START
        task_BTN_START,
        "task_BTN_START",
        4096,
        &ucParameterToPass,
        1,
        &xHandle_BTN_START); 

    xTaskCreate(        //TAREA BOTON STOP
        task_BTN_STOP,
        "task_BTN_STOP",
        2048,
        &ucParameterToPass,
        1,
        &xHandle_BTN_STOP);

    xTaskCreate(        //TAREA BOTON MODO
        task_BTN_MODO,
        "task_BTN_MODO",
        2048,
        &ucParameterToPass,
        1,
        &xHandle_BTN_MODO);

    return;
}

void create_queue ()    //TAREA QUE CREA LAS QUEUES
{   
    queue_I2C_LCD=xQueueCreate(5,1);
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------TAREAS------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void vTaskl_LED_blink(void *pvParameters)
{
    while (1)
    {
        led1=!led1;
        gpio_set_level(LED_1, led1);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

void task_LCD_controller(void *pvParameters)    //TAREA CONTROLLADORA DEL LCD VACIAAAAAAAAAAAAAAAA
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_PID (void *pvParameters)  //CALCULA el PID que se debe aplicar. Muestra los parámetros por consola
{
    while (1) {

        esp_err_t err = as5600_get_angle_degrees(as5600_dev, &angulo_actual);    //Lee angulo del encoder

        if (err != ESP_OK) ESP_LOGE(ESP_LOGI_TAG, "Error leyendo AS5600: %s", esp_err_to_name(err)); //Detecta si hay error en la lectura del ángulo

        float error=angulo_deseado-angulo_actual;   //Crea la variable de error

        error=normalizar_error(error);  //Normalizo el error para que vaya por el camino mas corto

        if (fabsf(error) < BANDA_ERROR) {  //Si el error está dentro del ángulo permitido. . . 
            MOTOR_stop();   //Frena motor
            pid_reset_ctrl_block_f(pid_ctrl);   //Frena el control PID
        } 
        else {
            ESP_ERROR_CHECK(pid_compute_f(pid_ctrl, error, &PID_output));   //Calculo la respuesta PID necesaria
            task_H_controller(PID_output);                                  //Aplico la respuesta PID al motor
        }
  
        ESP_LOGI(ESP_LOGI_TAG, 
            "Angulo actual: %.2f | deseado: %.2f | error: %.2f | salida PID: %.2f", 
            angulo_actual, angulo_deseado, error, PID_output);   //Muestro valores relevantes

        vTaskDelay(pdMS_TO_TICKS(PID_time));
    }
}

void task_encoder_controller(void *pvParameters)    //TAREA CONTROLADORA DEL ENCODER VACIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
{
    while (1){
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_I2C_guard ()  //VACIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
{
}

float normalizar_error(float error)  //Función para que el motor siempre vaya por el camino mas corto
{
    while (error>180.0f)  error -= 360.0f;
    while (error<-180.0f) error += 360.0f;
    return error;
}

void task_H_controller(float pid_output) //Aplica la respuesta PID necesaria al motor
{
    float magnitud = fabsf(pid_output); //Calcula valor absoluto de la salida generada por el PID
    
    if (!activo) pid_output=0.0;        //Si se desactivó  el PID. . . 

    if (magnitud < MOTOR_MIN_DUTY && magnitud > 0.0f)  magnitud = MOTOR_MIN_DUTY;   //Si la salida es menor que el MIN_DUTY...

    if (magnitud >= LEDC_DUTY_MAX)  magnitud = LEDC_DUTY_MAX;     //Si la salida es mayor que el MAX_DUTY...
       
    if (magnitud>=LEDC_DUTY_OBS){ OBS_detect++; }     //Para implementar la detección de obstaculo
    else {OBS_detect=0;}

    task_current_sens();

    if (OBS_flag) {
        pid_output *=-1.0f;   //le cambio el signo al pid para que vaya para el otro lado
        OBS_flag=0;
        OBS_detect=0;
    }

    if (pid_output > 0.0f) {    //Si la salida es positiva... HORARIO
        gpio_set_level(L298N_IN1, 1);
        gpio_set_level(L298N_IN2, 0);

    } 
    else if (pid_output < 0.0f) { //Si la salida es negativa... ANTIHORARIO
        gpio_set_level(L298N_IN1, 0);
        gpio_set_level(L298N_IN2, 1);

    } 
    else {        //Si la salida es CERO...
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

void MOTOR_stop ()  //FRENA el motor
{
    gpio_set_level(L298N_IN1, 0);
    gpio_set_level(L298N_IN2, 0);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    return;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------INTERRUPCIONES DE BOTONES---------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------------------


void IRAM_ATTR isr_BTN_ORIGEN(void *arg)    //Interrupción de boton ORIGEN
{
    if (!rebote){
        rebote=0;
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
    xTaskResumeFromISR(xHandle_BTN_STOP);
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
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskSuspend(NULL);
    }
}

void task_BTN_START (void *pvParameters)  //Función del boton START:
{
    while (1){
        activo=!activo;
        as5600_set_zero_position(as5600_dev);
        as5600_get_angle_degrees(as5600_dev, &angulo_deseado);
        vTaskDelay(pdMS_TO_TICKS(500)); //para antirrebote
        rebote=0;
        vTaskSuspend(NULL);
    }
}

void task_BTN_STOP (void *pvParameters)  //Cambia el angulo deseado a la posición actual del eje
{
    while (1){
        as5600_get_angle_degrees(as5600_dev, &angulo_deseado);
        vTaskDelay(pdMS_TO_TICKS(100));
        vTaskSuspend(NULL);
    }
}

void task_BTN_MODO (void *pvParameters)  //Función del boton MODO: Modifica perfiles mediante los coeficientes PID
{
    while (1){
        modo=!modo;
        vTaskDelay(pdMS_TO_TICKS(100));
        rebote=0;
        vTaskSuspend(NULL);
    }
}