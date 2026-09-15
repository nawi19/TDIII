#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <string.h>
#include "driver/uart.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "as5600.h"
#include "pid_ctrl.h"
#include "hd44780.h"    

#define L298N_IN1_GPIO           3     
#define L298N_IN2_GPIO           8     
#define L298N_ENA_GPIO           18

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_FREQ                100000     
#define LCD_DIR                 0x27
#define I2C_MASTER_SDA_IO       5
#define I2C_MASTER_SCL_IO       4
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_CHN                 0
#define ESP_LOGI_TAG            "Nahui"

#define LEDC_TIMER               LEDC_TIMER_0
#define LEDC_MODE                LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL             LEDC_CHANNEL_0
#define LEDC_DUTY_RES            LEDC_TIMER_10_BIT   // 0-1023
#define LEDC_FREQUENCY           1000                // 1 kHz 
#define LEDC_DUTY_MAX            1023.0f

#define MOTOR_MIN_DUTY           100.0f
#define ERROR_DEADBAND_DEG       0.5f

#define ANGULO_DESEADO_MIN       -360.0f
#define ANGULO_DESEADO_MAX        360.0f

#define UART_PORT      UART_NUM_1
#define TXD_PIN        (GPIO_NUM_0)
#define RXD_PIN        (GPIO_NUM_45)
#define BUF_SIZE       1024
#define UART_LINE_BUF_SIZE 64
#define TAGI            "UART_CP2102"

#define NVS_NAMESPACE   "motor_cfg"
#define NVS_KEY_ANGULO  "angulo_des"

#define PIN_ORIGEN 39       //DEFINES de botones
#define PIN_START 40        //DEFINES de botones
#define PIN_STOP 41         //DEFINES de botones
#define PIN_MODO 42         //DEFINES de botones

static lcd_bus_hd44780_t *LCD1_BUS = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static as5600_handle_t as5600_dev = NULL;
static const char *TAG = "MOTOR_ANGULO";
static pid_ctrl_block_handle_f_t pid_ctrl = NULL;

QueueHandle_t queue_I2C_LCD;

/*void isr_BTN_ORIGEN(void *arg);             //INTERRUPCIONES DE BOTONES
void isr_BTN_START(void *arg);              //INTERRUPCIONES DE BOTONES
void isr_BTN_STOP(void *arg);               //INTERRUPCIONES DE BOTONES
void isr_BTN_MODO(void *arg);               //INTERRUPCIONES DE BOTONES
*/

//Estructura con las ganancias y perfiles del PID
typedef struct {
    const char *nombre;
    float kp;
    float ki;
    float kd;
} perfil_pid_t;

static const perfil_pid_t perfiles[] = {
    [0] = { .nombre = "LENTO",  .kp = 4.5f, .ki = 0.2f, .kd = 1.7f },
    [1] = { .nombre = "RAPIDO", .kp = 9.0f, .ki = 0.2f, .kd = 3.0f },
};
#define NUM_PERFILES (sizeof(perfiles)/sizeof(perfiles[0]))

//Estructura para mandar por cola hacia el UART
typedef enum { CMD_ANGULO, CMD_PERFIL } cmd_type_t;

typedef struct {
    cmd_type_t tipo;
    float angulo;
    int   perfil;
} uart_cmd_t;

//Estructura para mandar por cola hacia el motor
typedef enum { MOTOR_CMD_PID, MOTOR_CMD_STOP } motor_cmd_type_t;

typedef struct {
    motor_cmd_type_t tipo;
    float pid_output;   // solo válido si tipo == MOTOR_CMD_PID
} motor_cmd_t;

//Definición de colas

static QueueHandle_t motor_queue;
static QueueHandle_t cmd_queue;
static QueueHandle_t encoder_queue;
static QueueHandle_t flash_queue;



/*TaskHandle_t xHandle_BTN_ORIGEN=NULL;
TaskHandle_t xHandle_BTN_START=NULL;
TaskHandle_t xHandle_BTN_STOP=NULL;
TaskHandle_t xHandle_BTN_MODO=NULL;*/

//Variables globales

static volatile int perfil_actual = 0;
static volatile float angulo_deseado = 0.0f;
static uint8_t ucParameterToPass;

/*bool activo=0;  //Indica si el PID se encuentra activo o no
volatile bool rebote=0; //Implementación de antirrebote

bool origen=0;      //VARIABLES DE BOTONES
bool start=0;       //VARIABLES DE BOTONES
bool stop=0;        //VARIABLES DE BOTONES
bool modo=0;        //VARIABLES DE BOTONES*/

//Configuracion 

static esp_err_t GPIO_config(void){

    gpio_config_t BOTONERA_io_conf = {       //CONFIGURACION GPIO DE LOS PINES DE BOTONERA
        .pin_bit_mask = (1ULL << PIN_ORIGEN | 1ULL << PIN_START | 1ULL << PIN_STOP | 1ULL << PIN_MODO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&BOTONERA_io_conf));

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << L298N_IN1_GPIO) | (1ULL << L298N_IN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(L298N_IN1_GPIO, 1);
    gpio_set_level(L298N_IN2_GPIO, 0);

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
        .gpio_num       = L298N_ENA_GPIO,
        .duty           = 0,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    return ESP_OK; 
}   

/*static void IRQ_config(void){

    gpio_install_isr_service(0);    //Instalar ISR service

    gpio_isr_handler_add(PIN_ORIGEN,    isr_BTN_ORIGEN, NULL); 
    gpio_isr_handler_add(PIN_START,     isr_BTN_START,  NULL);
    gpio_isr_handler_add(PIN_STOP,      isr_BTN_STOP,   NULL); 
    gpio_isr_handler_add(PIN_MODO,      isr_BTN_MODO,   NULL); 

}*/

static void Usart_config(){

uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Instalar driver con buffer de recepción
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    }

void LCD_init() //Inicialización del LCD E INICIALIZA EL I2C
{
    LCD1_BUS = lcd_bus_pcf8574_i2c_create( 
        I2C_CHN,
        LCD_DIR,
        I2C_MASTER_SDA_IO,
        I2C_MASTER_SCL_IO,
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

static void pid_init(void)
{
    pid_ctrl_parameter_f_t pid_params = {
        .kp = perfiles[perfil_actual].kp,
        .ki = perfiles[perfil_actual].ki,
        .kd = perfiles[perfil_actual].kd,
        .max_output = LEDC_DUTY_MAX,
        .min_output = -LEDC_DUTY_MAX,
        .max_integral = LEDC_DUTY_MAX,   // anti-windup
        .min_integral = -LEDC_DUTY_MAX,  // anti-windup
        .cal_type = PID_CAL_TYPE_POSITIONAL,
    };
 
    pid_ctrl_config_f_t pid_config = {
        .init_param = pid_params,
    };
 
    ESP_ERROR_CHECK(pid_new_control_block_f(&pid_config, &pid_ctrl));
}

static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

//Funciones

static float normalizar_error_angular(float error)
{

    while (error > 180.0f)  error -= 360.0f;
    while (error < -180.0f) error += 360.0f;
    return error;
}

static void guardar_angulo_deseado(float angulo)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir NVS para guardar: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, NVS_KEY_ANGULO, &angulo, sizeof(angulo));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error guardando angulo en NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

static float cargar_angulo_deseado(void)
{
    nvs_handle_t handle;
    float angulo = 0.0f; // valor por defecto si es la primera vez

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No hay config previa en NVS, usando default (%.2f)", angulo);
        return angulo;
    }

    size_t size = sizeof(angulo);
    err = nvs_get_blob(handle, NVS_KEY_ANGULO, &angulo, &size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo leer angulo de NVS, usando default");
        angulo = 0.0f;
    } else {
        ESP_LOGI(TAG, "Angulo deseado recuperado de NVS: %.2f", angulo);
    }

    nvs_close(handle);
    return angulo;
}
//Tareas

static void Encoder_task(void *pvParameters)
{
    float angulo_actual = 0.0f;

    while (1) {
        esp_err_t err = as5600_get_angle_degrees(as5600_dev, &angulo_actual);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error leyendo AS5600: %s", esp_err_to_name(err));
        } else {
            xQueueOverwrite(encoder_queue, &angulo_actual);
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // frecuencia de muestreo del encoder
    }
}

static void motor_task(void *pvParameters)
{
    motor_cmd_t cmd;

    while (1) {
        // Bloquea hasta que llegue un comando nuevo
        if (xQueueReceive(motor_queue, &cmd, portMAX_DELAY) == pdTRUE) {

            ESP_LOGI("MOTOR_TASK", "cmd recibido: tipo=%d pid_output=%.2f",
             cmd.tipo, cmd.pid_output);

            if (cmd.tipo == MOTOR_CMD_STOP) {
                gpio_set_level(L298N_IN1_GPIO, 0);
                gpio_set_level(L298N_IN2_GPIO, 0);
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                continue;
            }

            float pid_output = cmd.pid_output;
            float magnitud = fabsf(pid_output);

            if (magnitud < MOTOR_MIN_DUTY && magnitud > 0.0f) {
                magnitud = MOTOR_MIN_DUTY;
            }
            if (magnitud > LEDC_DUTY_MAX) {
                magnitud = LEDC_DUTY_MAX;
            }

            if (pid_output > 0.0f) {
                gpio_set_level(L298N_IN1_GPIO, 1);
                gpio_set_level(L298N_IN2_GPIO, 0);
            } else if (pid_output < 0.0f) {
                gpio_set_level(L298N_IN1_GPIO, 0);
                gpio_set_level(L298N_IN2_GPIO, 1);
            } else {
                gpio_set_level(L298N_IN1_GPIO, 0);
                gpio_set_level(L298N_IN2_GPIO, 0);
            }

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (uint32_t)magnitud);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            ESP_LOGI("MOTOR_TASK", "Aplicando duty=%.0f dir_IN1=%d dir_IN2=%d",
         magnitud, gpio_get_level(L298N_IN1_GPIO), gpio_get_level(L298N_IN2_GPIO));
        }
    }
}

static void uart_task(void *pvParameters)
{
    uint8_t data[BUF_SIZE];
    char line_buf[UART_LINE_BUF_SIZE]; //buffer para almacenar la linea completa de entrada
    int line_pos = 0; //posicion actual en el buffer de linea
 
    char InitMsg[] = "Ingrese el angulo deseado (entre -360 y 360 grados) seguido de Enter.\r\n"
                  "Ingrese P1 (lento) o P2 (rapido) para cambiar el perfil de movimiento.\r\n";
    uart_write_bytes(UART_PORT, InitMsg, strlen(InitMsg));
 
    while (1) {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(100)); //lee el angulo ingresado
 
        if (len > 0) { //cuando la funcion uart_read_bytes() devuelve una cantidad de bytes comienza
            for (int i = 0; i < len; i++) {//recorre cada caracter recibido
                char c = (char)data[i]; 
 
                if (c == '\n' || c == '\r') { //solo entra en el bloque al terminar de leer todos los caracteres del angulo ingresado
                    if (line_pos > 0) { //si hay algo en el buffer de linea, se procesa
                        line_buf[line_pos] = '\0';
 
                         if (line_pos >= 2 && line_buf[0] == 'P' &&
                            (line_buf[1] == '1' || line_buf[1] == '2')) {

                            uart_cmd_t cmd = { .tipo = CMD_PERFIL, .perfil = line_buf[1] - '1' };
                            xQueueSend(cmd_queue, &cmd, 0);
                            const char *ok = "OK: perfil actualizado\r\n";
                            uart_write_bytes(UART_PORT, ok, strlen(ok));

                        } else {
                            char *endptr = NULL;
                            float nuevo_angulo = strtof(line_buf, &endptr);

                            if (endptr == line_buf) {
                                ESP_LOGW(TAGI, "Dato invalido: '%s'", line_buf);
                                const char *err = "ERR: dato invalido\r\n";
                                uart_write_bytes(UART_PORT, err, strlen(err));
                            } else if (nuevo_angulo < ANGULO_DESEADO_MIN ||
                                       nuevo_angulo > ANGULO_DESEADO_MAX) {
                                ESP_LOGW(TAGI, "Valor fuera de rango: %s", line_buf);
                                const char *err = "ERR: fuera de rango\r\n";
                                uart_write_bytes(UART_PORT, err, strlen(err));
                            } else {
                                uart_cmd_t cmd = { .tipo = CMD_ANGULO, .angulo = nuevo_angulo };
                                xQueueSend(cmd_queue, &cmd, 0);
                                ESP_LOGI(TAGI, "Nuevo angulo deseado: %.2f", nuevo_angulo);

                                char resp[32];
                                int resp_len = snprintf(resp, sizeof(resp), "OK: %.2f\r\n", nuevo_angulo);
                                uart_write_bytes(UART_PORT, resp, resp_len);
                            }
                        }
 
                        line_pos = 0; //reinicia la posicion del buffer de linea para la proxima entrada
                    }
                } else if (line_pos < (UART_LINE_BUF_SIZE - 1)) { //si el caracter recibido no es un salto de linea y el buffer de linea no esta lleno, se agrega el caracter al buffer
                    line_buf[line_pos++] = c;
                }
                // si la linea es mas larga que el buffer, se descartan
                // los caracteres extra hasta el proximo '\n'/'\r'
            }
        }
    }
}

static void control_task(void *pvParameters)
{

    float angulo_actual = 0.0f;
    float pid_output = 0.0f;

    while (1) {

        // --- Procesar comandos pendientes de UART (angulo o perfil) ---
        uart_cmd_t cmd;
        while (xQueueReceive(cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd.tipo == CMD_ANGULO) {
                angulo_deseado = cmd.angulo;
                float angulo_deseado_flash = angulo_deseado;
                xQueueOverwrite(flash_queue, &angulo_deseado_flash); // Guardar en NVS
            } else if (cmd.tipo == CMD_PERFIL && cmd.perfil < NUM_PERFILES) {
                perfil_actual = cmd.perfil;

                pid_ctrl_parameter_f_t nuevos_params = {
                    .kp = perfiles[perfil_actual].kp,
                    .ki = perfiles[perfil_actual].ki,
                    .kd = perfiles[perfil_actual].kd,
                    .max_output = LEDC_DUTY_MAX,
                    .min_output = -LEDC_DUTY_MAX,
                    .max_integral = LEDC_DUTY_MAX,
                    .min_integral = -LEDC_DUTY_MAX,
                    .cal_type = PID_CAL_TYPE_POSITIONAL,
                };
                ESP_ERROR_CHECK(pid_update_parameters_f(pid_ctrl, &nuevos_params));
                pid_reset_ctrl_block_f(pid_ctrl);
                ESP_LOGI(TAG, "Perfil cambiado a: %s", perfiles[perfil_actual].nombre);
            }
        }

        if (xQueuePeek(encoder_queue, &angulo_actual, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Sin dato de encoder, usando ultimo valor: %.2f", angulo_actual);
    }

        float error = angulo_deseado - angulo_actual;
        error = normalizar_error_angular(error);

         if (fabsf(error) < ERROR_DEADBAND_DEG) {
            motor_cmd_t cmd = { .tipo = MOTOR_CMD_STOP };
            xQueueSend(motor_queue, &cmd, 0);
            pid_reset_ctrl_block_f(pid_ctrl);
        } else {
            ESP_ERROR_CHECK(pid_compute_f(pid_ctrl, error, &pid_output));
            motor_cmd_t cmd = { .tipo = MOTOR_CMD_PID, .pid_output = pid_output };
            xQueueSend(motor_queue, &cmd, 0);
        }
  
        ESP_LOGI(TAG, "Angulo actual: %.2f | deseado: %.2f | error: %.2f | salida PID: %.2f", angulo_actual, angulo_deseado, error, pid_output);

        // Lineas adicionales solo para Teleplot (no usan el prefijo de ESP_LOG)
        printf(">angulo_actual:%.2f\n", angulo_actual);
        printf(">angulo_deseado:%.2f\n", angulo_deseado);
        //printf(">error:%.2f\n", error);

        vTaskDelay(pdMS_TO_TICKS(10));
        
    }
}

static void flashtask(void *pvParameters){

    float angulo_deseado_flash;

    while(1){

        if(xQueueReceive(flash_queue, &angulo_deseado_flash, portMAX_DELAY) == pdTRUE){

            guardar_angulo_deseado(angulo_deseado_flash);
            ESP_LOGI(TAG, "Angulo deseado guardado en NVS: %.2f", angulo_deseado_flash);
        }
    }

}

//Interrupciones de botones

/*void IRAM_ATTR isr_BTN_ORIGEN(void *arg)    //Interrupción de boton ORIGEN
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

        float angulo_leido;
        as5600_get_angle_degrees(as5600_dev, &angulo_leido);
        angulo_deseado = angulo_leido;

        vTaskDelay(pdMS_TO_TICKS(500)); //para antirrebote
        rebote=0;
        vTaskSuspend(NULL);
    }
}

void task_BTN_STOP (void *pvParameters)  //Cambia el angulo deseado a la posición actual del eje
{
    while (1){

        float angulo_leido;
        as5600_get_angle_degrees(as5600_dev, &angulo_leido);
        angulo_deseado = angulo_leido;

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
*/
void app_main(void){

    GPIO_config();
    LCD_init();
    AS5600_init();
    pid_init();
    Usart_config();
    nvs_init();

    cmd_queue = xQueueCreate(5, sizeof(uart_cmd_t));
    if (cmd_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cmd_queue");
    }

    motor_queue = xQueueCreate(5, sizeof(motor_cmd_t));
    if (motor_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear motor_queue");      
    }

    encoder_queue = xQueueCreate(1, sizeof(float));
    if (encoder_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear encoder_queue"); 
    }

    flash_queue = xQueueCreate(1, sizeof(float));
    if( flash_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear flash_queue");
    }   

    angulo_deseado = cargar_angulo_deseado();

    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 5, NULL);
    xTaskCreate(motor_task, "motor_task", 4096, NULL, 5, NULL);
    xTaskCreate(Encoder_task, "Encoder_task", 4096, NULL, 5, NULL);
    xTaskCreate(flashtask, "flashtask", 4096, NULL, 2, NULL);

    //tareas de los botones 

    /*xTaskCreate(task_BTN_ORIGEN,"task_BTN_ORIGEN",2048,&ucParameterToPass,1,&xHandle_BTN_ORIGEN);
    xTaskCreate(task_BTN_START,"task_BTN_START",2048,&ucParameterToPass,1,&xHandle_BTN_START);
    xTaskCreate(task_BTN_STOP,"task_BTN_STOP",2048,&ucParameterToPass,1,&xHandle_BTN_STOP);
    xTaskCreate(task_BTN_MODO,"task_BTN_MODO",2048,&ucParameterToPass,1,&xHandle_BTN_MODO);

    IRQ_config();*/

}