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

#define L298N_IN1_GPIO           3     
#define L298N_IN2_GPIO           8     
#define L298N_ENA_GPIO           18

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_SDA_IO       5
#define I2C_MASTER_SCL_IO       4
#define I2C_MASTER_FREQ_HZ      400000

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

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static as5600_handle_t as5600_dev = NULL;
static const char *TAG = "MOTOR_ANGULO";
static pid_ctrl_block_handle_f_t pid_ctrl = NULL;


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

typedef enum { CMD_ANGULO, CMD_PERFIL } cmd_type_t;

typedef struct {
    cmd_type_t tipo;
    float angulo;
    int   perfil;
} uart_cmd_t;

static QueueHandle_t cmd_queue;
static volatile int perfil_actual = 0;
static volatile float angulo_deseado = 0.0f;

static esp_err_t Config(void){

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

    static void as5600_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus_handle));

    as5600_i2c_config_t as5600_config = {
        .scl_speed_hz = I2C_MASTER_FREQ_HZ
    };
    ESP_ERROR_CHECK(as5600_new_sensor(i2c_bus_handle, &as5600_config, &as5600_dev));

    // Chequeo opcional de presencia del iman
    as5600_magnet_status_t status;
    if (as5600_get_magnet_status(as5600_dev, &status) == ESP_OK) {
        ESP_LOGI(TAG, "Estado iman AS5600: %d", (int)status);
    }
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

static void Motor_stop(void)
{
    gpio_set_level(L298N_IN1_GPIO, 0);
    gpio_set_level(L298N_IN2_GPIO, 0);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static float normalizar_error_angular(float error)
{

    while (error > 180.0f)  error -= 360.0f;
    while (error < -180.0f) error += 360.0f;
    return error;
}

static void Motor_aplicar_pid(float pid_output)
{
    float magnitud = fabsf(pid_output);
 
    if (magnitud < MOTOR_MIN_DUTY && magnitud > 0.0f) {
        magnitud = MOTOR_MIN_DUTY;
    }
    if (magnitud > LEDC_DUTY_MAX) {
        magnitud = LEDC_DUTY_MAX;
    }
 
    if (pid_output > 0.0f) {
        // Avanzar
        gpio_set_level(L298N_IN1_GPIO, 1);
        gpio_set_level(L298N_IN2_GPIO, 0);
    } else if (pid_output < 0.0f) {
        // Retroceder
        gpio_set_level(L298N_IN1_GPIO, 0);
        gpio_set_level(L298N_IN2_GPIO, 1);
    } else {
        gpio_set_level(L298N_IN1_GPIO, 0);
        gpio_set_level(L298N_IN2_GPIO, 0);
    }
 
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (uint32_t)magnitud);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void uart_task(void *arg)
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

static void control_task(void *arg)
{

    float angulo_actual = 0.0f;
    float pid_output = 0.0f;

    while (1) {

        // --- Procesar comandos pendientes de UART (angulo o perfil) ---
        uart_cmd_t cmd;
        while (xQueueReceive(cmd_queue, &cmd, 0) == pdTRUE) {
            if (cmd.tipo == CMD_ANGULO) {
                angulo_deseado = cmd.angulo;
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

        esp_err_t err = as5600_get_angle_degrees(as5600_dev, &angulo_actual);

        float error = angulo_deseado - angulo_actual;
        error = normalizar_error_angular(error);

         if (fabsf(error) < ERROR_DEADBAND_DEG) {
            Motor_stop();
            pid_reset_ctrl_block_f(pid_ctrl);
        } else {
            ESP_ERROR_CHECK(pid_compute_f(pid_ctrl, error, &pid_output));
            Motor_aplicar_pid(pid_output);
        }
  
        ESP_LOGI(TAG, "Angulo actual: %.2f | deseado: %.2f | error: %.2f | salida PID: %.2f", angulo_actual, angulo_deseado, error, pid_output);

        // Lineas adicionales solo para Teleplot (no usan el prefijo de ESP_LOG)
        printf(">angulo_actual:%.2f\n", angulo_actual);
        printf(">angulo_deseado:%.2f\n", angulo_deseado);
        //printf(">error:%.2f\n", error);

        vTaskDelay(pdMS_TO_TICKS(10));
        
    }
}


void app_main(void){

    ESP_ERROR_CHECK(Config());
    as5600_init();
    pid_init();
    Usart_config();

    cmd_queue = xQueueCreate(5, sizeof(uart_cmd_t));
    if (cmd_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear cmd_queue");
    }

    xTaskCreate(control_task, "control_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 5, NULL);

}
