#include "MainTask.h"
#include "CommTask.h"
#include "CtrlTask.h"
#include "FlashTask.h"
#include "InterruptTask.h"
#include "KeyTask.h"
#include "MesgTask.h"
#include "port_event.h"
#include "DigitalTubeTask.h"
#include "iwdg.h"
#include "InterruptTask.h"
#include "gpio.h"

#define SYSLIGHT_BLINK_TIME 500

/* 兼容旧工程中仍参与编译但已退出运行路径的灯效模块，第二阶段删除对应工程项后再移除。 */
Scene_t Scene = IdleScene;
Event_Handle_t Event;

extern Tx_HandleTypeDef Tx3;
extern Motor_Hoolle Motor_Hoolle1, Motor_Hoolle2;
extern Motor_Card Card;

/* 中文注释：每关独立记录电磁阀次数与奖励命中位。 */
static GameState_t GameState = GAME_IDLE;
static uint16_t GameLevel = 1U;
static uint8_t GameValveCount = 0U;
static uint8_t GameRewardMask = 0U;
static uint8_t GameFirstBallSent = 0U;
static uint8_t GameValveActive = 0U;
static uint32_t GameValveTick = 0U;

static void Game_ValveOff(void)
{
    HAL_GPIO_WritePin(GPIOB, LedOutput1_Pin | LedOutput2_Pin, GPIO_PIN_RESET);
    GameValveActive = 0U;
}

static void Game_NextLevel(void)
{
    /* 中文注释：免费下一关只清关内记录，不清PE2累计珠数。 */
    GameValveCount = 0U;
    GameRewardMask = 0U;
    GameFirstBallSent = 0U;
    Game_ValveOff();
    GameState = GAME_PLAYING;
}

void Game_ButtonPressed(void)
{
    if (GameState == GAME_IDLE || GameState == GAME_FAILED ||
        GameState == GAME_SUCCESS || GameFirstBallSent == 0U)
    {
        /* 中文注释：先确认电机1可出珠，再决定开新局及扣除付费余额。 */
        if (Motor_Hoolle1.ManualRun != 0U ||
            Motor_Hoolle1.Motor.state != DEVICE_STATE_IDLE ||
            Motor_Hoolle1.Hoolle_num != 0U)
            return;

        if (GameState == GAME_IDLE || GameState == GAME_FAILED)
        {
            if (GameBallCount < 5U)
                return;
            /* 中文注释：只有付费开新局扣5颗，免费续关不扣；与PE2中断计数互斥。 */
            __disable_irq();
            GameBallCount -= 5U;
            __enable_irq();
            GameLevel = 1U;
            Game_NextLevel();
        }
        else if (GameState == GAME_SUCCESS)
        {
            /* 中文注释：成功后免费进入下一关，每关首次按键仍实际吐1颗。 */
            Game_NextLevel();
        }

        if (GameState != GAME_PLAYING)
            return;
        Hoolle_Output(&Motor_Hoolle1, 1U);
        GameFirstBallSent = 1U;
        return;
    }

    if (GameState != GAME_PLAYING ||
        GameValveCount >= 6U || GameValveActive != 0U)
        return;

    /* 中文注释：两组电磁阀共同动作100ms，每关最多6次。 */
    GameValveCount++;
    GameValveTick = HAL_GetTick();
    HAL_GPIO_WritePin(GPIOB, LedOutput1_Pin | LedOutput2_Pin, GPIO_PIN_SET);
    GameValveActive = 1U;
}

void Game_RewardTriggered(uint8_t index)
{
    static const uint8_t RewardCounts[6] = {5U, 10U, 2U, 1U, 2U, 3U};
    uint8_t bit;
    if (GameState != GAME_PLAYING || index >= 6U)
        return;
    bit = (uint8_t)(1U << index);
    if ((GameRewardMask & bit) != 0U)
        return;
    GameRewardMask |= bit;
    /* 中文注释：前三路使用吐珠电机2，后三路均使用PC4/PE5卡片机1。 */
    if (index < 3U)
        Hoolle_Output(&Motor_Hoolle2, RewardCounts[index]);
    else
        Card_Output(&Card, RewardCounts[index]);
}

void Game_EndTriggered(void)
{
    if (GameState != GAME_PLAYING)
        return;
    Game_ValveOff();
    if (GameRewardMask == 0x3FU)
    {
        if (GameLevel < 9999U)
            GameLevel++;
        GameState = GAME_SUCCESS;
    }
    else
    {
        GameLevel = 1U;
        GameState = GAME_FAILED;
    }
}

void Game_Stop(void)
{
    /* 中文注释：外部停止命令清除本关动作状态，禁止后续按键继续驱动电磁阀。 */
    Game_ValveOff();
    GameState = GAME_IDLE;
    GameValveCount = 0U;
    GameRewardMask = 0U;
    GameFirstBallSent = 0U;
}

void Game_Task(void)
{
    if (GameValveActive != 0U &&
        (uint32_t)(HAL_GetTick() - GameValveTick) >= 100U)
        Game_ValveOff();
}


void System_Reset(void)
{
    __disable_irq();
    HAL_NVIC_SystemReset();
}

static void SystemLight_Task(void)
{
    static uint32_t time = 0;
    if (HAL_GetTick() - time > SYSLIGHT_BLINK_TIME)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        time = HAL_GetTick();
    }
}

void Main_Init(void)
{
    FlashTask_Init();
    CommInit();
    Device_Init();
    HoolleInput_FilterInit();
    KeyAll_Init();

    /* 中文注释：主板本地数码管继续保留，控台数码管通过USART3同步接收显示数据。 */
    DigitalTubeTask_Init();

    /*
     * 控台上电初始化沿用小丑协议：
     * 0x00请求版本，0x02灯带亮度，0x03场景，0x04控台亮度。
     */
    Comm_SendMesg_FillData(&Tx3, Board_to_Ctrl, CTRL_CMD_GET_VERSION, 0x00, 0x00);

    if (Setting.LightBelt_Lightness <= CTRL_BRIGHTNESS_MAX)
        Comm_SendMesg_FillData(&Tx3,
                               Board_to_Ctrl,
                               CTRL_CMD_LIGHT_BELT_BRIGHTNESS,
                               Setting.LightBelt_Lightness,
                               0x00);

    Comm_SendMesg_FillData(&Tx3,
                           Board_to_Ctrl,
                           CTRL_CMD_LIGHT_SCENE,
                           0x00,
                           (uint8_t)Scene);

    if (Setting.Ctrl_Lightness <= CTRL_BRIGHTNESS_MAX)
        Comm_SendMesg_FillData(&Tx3,
                               Board_to_Ctrl,
                               CTRL_CMD_BRIGHTNESS,
                               Setting.Ctrl_Lightness,
                               0x00);
}

void Main_Task(void)
{
    CommTask();
    HAL_IWDG_Refresh(&hiwdg);
    FlashTask();
    HAL_IWDG_Refresh(&hiwdg);
    Key_Task();
    Game_Task();
    HAL_IWDG_Refresh(&hiwdg);
    /* 中文注释：旧弹界本地灯效任务已从主循环移除。 */
    CtrlTask();
    HAL_IWDG_Refresh(&hiwdg);
    Mesg_Task();
    HAL_IWDG_Refresh(&hiwdg);

    /* 中文注释：数码管为主板直连模块，保持本地刷新任务。 */
    DigitalTube_Task();
    SystemLight_Task();
}
