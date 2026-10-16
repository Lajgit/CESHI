#include "DigitalTubeTask.h"
#include "InterruptTask.h"
#include "CtrlTask.h"
#include "MainTask.h"

extern Motor_Hoolle Motor_Hoolle2;

#define DigitalTube_Refresh_Time 25

static uint8_t DigitalBuffer[10] = {0};
DigitalTube_t DigitalTube;

/*
 * 三位数码管兼容标记：
 * 1 = 开启左移一位显示，例如安卓下发123时按1230写入；
 * 0 = 保持原四位显示逻辑。
 */
uint8_t DigitalTubeShiftEnable = 1U;

void DigitalTubeTask_Init(void)
{
    DigitalTube_Init_t Init;

    Init.hspi = &hspi2;
    Init.Buffer = DigitalBuffer;
    Init.bit_num = sizeof(DigitalBuffer);
    Init.LE_GPIO = Tube_RCLK_GPIO_Port;
    Init.LE_Pin = Tube_RCLK_Pin;
    Init.CODE_CA = DIGITAL3BIT_CODE_CA;

    DigitalTube_Init(&DigitalTube, Init);
    /* 中文注释：正式版上电全部显示0，后续由游戏数据刷新。 */
    for (uint8_t i = 0U; i < 10U; i++)
        DigitalBuffer[i] = DIGITAL3BIT_CODE_CA[0];
    DigitalTube.Refresh(&DigitalTube);

    /* 中文注释：首帧数据完成锁存后再拉低SEG_OE，使能数码管输出。 */
    HAL_GPIO_WritePin(Tube_OE_GPIO_Port, Tube_OE_Pin, GPIO_PIN_RESET);
}

/*
 * @brief 设置数码管显示值
 * @note 三位兼容开启时，将0~999左移一位写入四位缓存。
 *       例如123按1230写入，使实际不存在的个位落在最后一位，
 *       三个可见数码管显示123。0需要单独写到原十位位置。
 *       大于999的值保持原四位处理，避免改变旧协议的超范围行为。
 */
void DigitalTube_SetDisplayNumber(uint32_t data)
{
    uint32_t display_data = data;

    if (DigitalTubeShiftEnable != 0U && data <= 999U)
    {
        if (data == 0U)
        {
            /*
             * 0乘10仍为0，原Set_Num会把0放在个位。
             * 三位硬件没有个位，因此直接按3位数据写入，
             * 让0显示在原十位对应的最右可见位置。
             */
            DigitalTube.Set_Num(&DigitalTube, 0, 0U, 3);
            DigitalTube.Refresh(&DigitalTube);
            return;
        }

        display_data = data * 10U;
    }

    DigitalTube.Set_Num(&DigitalTube, 0, display_data, 4);
    DigitalTube.Refresh(&DigitalTube);
}

/* 中文注释：逻辑发送顺序CENTER(余额2位)、LEFT(关卡4位)、RIGHT(电机2待吐珠4位)；
 * 保留已验证的DigitalBuffer[9-i]级联映射，全部固定补零。
 */
static void DigitalTube_WriteNumber(uint8_t start, uint8_t digits, uint16_t value)
{
    for (uint8_t pos = 0U; pos < digits; pos++)
    {
        DigitalBuffer[9U - (uint8_t)(start + digits - 1U - pos)] =
            DIGITAL3BIT_CODE_CA[value % 10U];
        value /= 10U;
    }
}

void DigitalTube_Task(void)
{
    static uint32_t time = 0U;
    static uint16_t old_balance = 0xFFFFU;
    static uint16_t old_level = 0xFFFFU;
    static uint16_t old_pending = 0xFFFFU;
    uint16_t balance, level, pending;

    if ((uint32_t)(HAL_GetTick() - time) < DigitalTube_Refresh_Time)
        return;
    time = HAL_GetTick();

    balance = GameBallCount;
    level = Game_GetDisplayLevel();
    pending = Motor_Hoolle2.Hoolle_num;

    if (balance != old_balance || level != old_level || pending != old_pending)
    {
        DigitalTube_WriteNumber(0U, 2U, (uint16_t)(balance % 100U));
        DigitalTube_WriteNumber(2U, 4U, level);
        DigitalTube_WriteNumber(6U, 4U, (uint16_t)(pending % 10000U));
        old_balance = balance;
        old_level = level;
        old_pending = pending;
        DigitalTube.Refresh(&DigitalTube);
    }
}
