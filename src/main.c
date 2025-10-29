/* * *********************************************************
Sample code to get communicate Microbit V2 with nrf Connect
This code is only for educational purposes
The code have more libraries than necessary due to future implementations
My device name is called "microbio" check out prj.conf to change the name
* ** ** * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(BLE, LOG_LEVEL_INF); // LOG_INF y LOG_ERR sustituyen a printk

// En esta seccion  generamos la interrupción para botón A (sw0)
#define BUTTON_A_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec bA= GPIO_DT_SPEC_GET(BUTTON_A_NODE, gpios);
static struct gpio_callback button_cb;

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                             uint32_t pins);


// Se define el UUID, pregunten(me) que little-endian y big-endian
// Importante, los UUID deben coincidir con los de la seccion GATT
static const struct bt_data ad[] = {
    // Zephyr soporta BLE pero no soporta BR/EDR
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    // El UUID esta espejeado para la transmisión
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, 
        0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 
        0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12)
};

// Para que aparezca el nombre correcto cuando se haga el scanning
static const struct bt_data sd[] = {
    // El nombre esta definido en el prj.conf
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_le_adv_param adv_params = {
    // Parametros de conexion, intervalos minimos y maximos.
    .options = BT_LE_ADV_OPT_CONN, 
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
};


// BLE: caracteristica
static uint8_t value = 0;
static struct bt_conn *current_conn;


// Habiltación de la notificación
// Pregunta(me) que hace el '?'
static void button_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Notificationes %s", value == BT_GATT_CCC_NOTIFY ? "habilitadas" : "deshabilitadas");
}


// GATT 
BT_GATT_SERVICE_DEFINE(button_svc,
    // UUID es igual que en la definicion de UUIDs
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(
        0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12, //  8 bytes inferiores
        0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12)), // 8 bytes superiores
        
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(
        // Este UUID es ligeramente distinto, en lugar de f0 comienza con f1
        // Recuerda que esta espejeado, asi que en realidad termina con f1 
        0xf1,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12, 
        0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12),
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, &value),
        
    BT_GATT_CCC(button_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

// Funciones (callbacks) de la conexion, ¿donde se estan llamando?
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Hay un fallo en la conexion:( (err %u)", err);
    } else {
        current_conn = bt_conn_ref(conn);
        LOG_INF("Conectado :) ");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Se desconectó por... (razon # %u)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    // Reinicia el advertising (Anda de migajero el BLE)
    int ret = bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret) {
        LOG_ERR("No se puedo reiniciar el advertising (err %d)", ret);
    } else {
        LOG_INF("Se logra sin problema el advertising");
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// ISR del buton A
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                             uint32_t pins)
{
    value++;
    LOG_INF("Boton A presionado %d veces.", value);

    // Avisar al telefono que hubo un cambio
    if (current_conn) {
        bt_gatt_notify(current_conn, &button_svc.attrs[2], &value, sizeof(value));
    } else {
        LOG_INF("No hay cliente que reciba");
    }
}

// Programa Principal
int main(void)
{
    int ret;

    LOG_INF("...Inicializacion del BLE...");

    // Revision del GPIO
    if (!device_is_ready(bA.port)) {
        LOG_ERR("El GPIO del boton no inicio");
        return 0;
    }

    // Configuracion del boton A como entrada PULL UP y set de la interrupcion
    ret = gpio_pin_configure_dt(&bA, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) return 0;

    gpio_init_callback(&button_cb, button_pressed, BIT(bA.pin));
    gpio_add_callback(bA.port, &button_cb);
    gpio_pin_interrupt_configure_dt(&bA, GPIO_INT_EDGE_TO_ACTIVE);
    

    // Habilitar el BLE
    ret = bt_enable(NULL);
    if (ret) {
        LOG_ERR("No funciono el BLE :( (err %d)", ret);
        return 0;
    }
    LOG_INF("Bluetooth OK");
   

    // Permitir que el BLE sea visible
    ret = bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret) {
        LOG_ERR("El Advertising no fue posible :( (err %d)", ret);
        return 0;
    }
    LOG_INF("El Advertising funciona, busca a: %s", CONFIG_BT_DEVICE_NAME);
    return 0;
   
}
