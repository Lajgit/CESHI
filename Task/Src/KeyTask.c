#include "KeyTask.h"
#include "CommTask.h"
#include "port_key.h"
#include "MainTask.h"
#include "main.h"

#define SETTING_BUTTON_COUNT 3U
#define PLAY_BUTTON_COUNT 4U

/*
 * 中文注释：后台设置按键按原理图K1/K2/K3编号：
 * K1=PB6，K2=PB7，K3=PD0，对外协议编号依次为0x01~0x03。
 */
static GPIO_TypeDef *SettingButton_Port[SETTING_BUTTON_COUNT] = {
    KeyBoard3_GPIO_Port,
    KeyBoard2_GPIO_Port,
    KeyBoard1_GPIO_Port,
};
/* 中文注释：数组名避免与main.h中旧的SettingButton_Pin宏重名。 */
static uint32_t SettingButton_Pins[SETTING_BUTTON_COUNT] = {
    KeyBoard3_Pin,
    KeyBoard2_Pin,
    KeyBoard1_Pin,
};

/* 中文注释：外接游玩/拍拍按键1~4分别接PD8/PD9/PD10/PD11，协议功能码为0x03。 */
static GPIO_TypeDef *PlayButton_Port[PLAY_BUTTON_COUNT] = {
    PlayButton1_GPIO_Port,
    PlayButton2_GPIO_Port,
    PlayButton3_GPIO_Port,
    PlayButton4_GPIO_Port,
};
static uint32_t PlayButton_Pins[PLAY_BUTTON_COUNT] = {
    PlayButton1_Pin,
    PlayButton2_Pin,
    PlayButton3_Pin,
    PlayButton4_Pin,
};

static Key_HandleTypeDef setting_button[SETTING_BUTTON_COUNT];
static Key_HandleTypeDef *setting_button_list[SETTING_BUTTON_COUNT];
static Key_HandleTypeDef play_button[PLAY_BUTTON_COUNT];
static Key_HandleTypeDef *play_button_list[PLAY_BUTTON_COUNT];

extern Tx_HandleTypeDef Tx1;

/*==============================后台设置按键==============================*/
static void SettingButton_ShortCallback(uint16_t key_id)
{
    /* 中文注释：0x04=设置按键，Data4=编号，ExpandCode=短按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_SHORT);
}

static void SettingButton_LongCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=长按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_LONG);
}

static void SettingButton_ReleaseCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=松开。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_SettingButton, key_id + 1U, KEY_EVENT_RELEASE);
}

static void SettingButton_Init(void)
{
    Key_InitTypeDef init;

    for (uint16_t i = 0; i < SETTING_BUTTON_COUNT; i++)
    {
        init.short_callback = SettingButton_ShortCallback;
        init.long_callback = SettingButton_LongCallback;
        init.release_callback = SettingButton_ReleaseCallback;
        init.debounce_time = KEY_DEBOUNCE_TIME;
        init.longpress_time = KEY_LONG_PRESS_TIME;
        init.trigger_frequnecy = 1;
        init.trigger_level = GPIO_PIN_RESET;
        init.key_id = i;
        init.port = SettingButton_Port[i];
        init.pin = SettingButton_Pins[i];

        Key_Init(&setting_button[i], init);
        setting_button_list[i] = &setting_button[i];
    }
}

/*==============================外接游玩按键==============================*/
static void PlayButton_ShortCallback(uint16_t key_id)
{
    /* 中文注释：0x03=拍拍按键，Data4=编号，ExpandCode=短按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Button, key_id + 1U, KEY_EVENT_SHORT);
}

static void PlayButton_LongCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=长按。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Button, key_id + 1U, KEY_EVENT_LONG);
}

static void PlayButton_ReleaseCallback(uint16_t key_id)
{
    /* 中文注释：ExpandCode=松开。 */
    Comm_SendMesg_FillData(&Tx1, Board_to_Android, t_Button, key_id + 1U, KEY_EVENT_RELEASE);
}

static void PlayButton_Init(void)
{
    Key_InitTypeDef init;

    for (uint16_t i = 0; i < PLAY_BUTTON_COUNT; i++)
    {
        init.short_callback = PlayButton_ShortCallback;
        init.long_callback = PlayButton_LongCallback;
        init.release_callback = PlayButton_ReleaseCallback;
        init.debounce_time = KEY_DEBOUNCE_TIME;
        init.longpress_time = KEY_LONG_PRESS_TIME;
        init.trigger_frequnecy = 1;
        init.trigger_level = GPIO_PIN_RESET;
        init.key_id = i;
        init.port = PlayButton_Port[i];
        init.pin = PlayButton_Pins[i];

        Key_Init(&play_button[i], init);
        play_button_list[i] = &play_button[i];
    }
}

/* 中文注释：八路游戏输入均为低电平有效，稳定15ms后立即产生一次按下事件，
   不等待微动释放；必须恢复高电平后才能再次触发。 */
static GPIO_TypeDef *GameInputPorts[8] = {GPIOD, GPIOD, GPIOD, GPIOD, GPIOB, GPIOB, GPIOE, GPIOD};
static uint16_t GameInputPins[8] = {GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10, GPIO_PIN_0,
                                   GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_6};
static uint8_t GameRaw[8];
static uint8_t GameStable[8];
static uint32_t GameChangeTick[8];

static void Game_InputCallback(uint16_t key_id)
{
    if (key_id < 6U)
        Game_RewardTriggered((uint8_t)key_id);
    else if (key_id == 6U)
        Game_EndTriggered();
    else
        Game_ButtonPressed();
}

static void GameInput_Init(void)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t level = (HAL_GPIO_ReadPin(GameInputPorts[i], GameInputPins[i]) == GPIO_PIN_SET) ? 1U : 0U;
        GameRaw[i] = level;
        GameStable[i] = level;
        GameChangeTick[i] = HAL_GetTick();
    }
}

static void GameInput_Scan(void)
{
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0U; i < 8U; i++)
    {
        uint8_t level = (HAL_GPIO_ReadPin(GameInputPorts[i], GameInputPins[i]) == GPIO_PIN_SET) ? 1U : 0U;
        if (level != GameRaw[i])
        {
            GameRaw[i] = level;
            GameChangeTick[i] = now;
        }
        else if (level != GameStable[i] &&
                 (uint32_t)(now - GameChangeTick[i]) >= KEY_DEBOUNCE_TIME)
        {
            GameStable[i] = level;
            if (level == 0U)
                Game_InputCallback(i);
        }
    }
}

void KeyAll_Init(void)
{
    GameInput_Init();
}

void Key_Task(void)
{
    GameInput_Scan();
}
