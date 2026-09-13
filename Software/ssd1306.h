#ifndef __SSD1306_H__
#define __SSD1306_H__

#include "main.h"
#include <string.h>

// 适配你的96*32分辨率OLED屏
#define OLED_WIDTH   96
#define OLED_HEIGHT  32

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t chr, uint8_t size);
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t *str, uint8_t size);
void OLED_ShowNum(uint8_t x, uint8_t y, float num, uint8_t len, uint8_t size);

#endif
