"""Compile and exercise the actual native TIM backend with CMSIS/HAL doubles."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[1]
s=(root/'ports/stm32/mcs_port_stm32.c').read_text()
backend=s[s.index('/* Native TIM quadrature:'):s.index('static int st_pwm_set16(')]
def function(signature):
 b=s.index(signature);i=s.index('{',b)+1;depth=1
 while depth: depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[b:i]
prelude=r'''
#include "mcs_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TIM_ENCODERMODE_TI12 3
#define TIM_SMCR_SMS 7
#define TIM_CR1_CEN 1
#define TIM_IT_UPDATE 1
#define TIM_FLAG_UPDATE 1
#define TIM_EGR_UG 1
#define TIM_CHANNEL_ALL 255
#define MCS_STM32_ENCODERS 4
#define MCS_STM32_TIMERS 4
typedef struct {uint32_t CR1,SMCR,EGR,CNT,ARR,PSC,SR,DIER;int wide;} TIM_TypeDef;
typedef struct {TIM_TypeDef* Instance;} TIM_HandleTypeDef;
typedef struct {TIM_HandleTypeDef* h;int a,b;uint32_t high,mask;uint8_t active;} mcs_stm32_encoder_t;
typedef struct {mcs_stm32_encoder_t encoder[4];struct {TIM_HandleTypeDef* h;} timer[4];uint32_t timer_count[4];uint8_t timer_once[4];} mcs_stm32_board_t;
static mcs_stm32_board_t board,*g_board=&board;
#define IS_TIM_32B_COUNTER_INSTANCE(i) ((i)->wide)
#define __HAL_TIM_SET_COUNTER(h,x) ((h)->Instance->CNT=(x))
#define __HAL_TIM_GET_COUNTER(h) ((h)->Instance->CNT)
#define __HAL_TIM_SET_PRESCALER(h,x) ((h)->Instance->PSC=(x))
#define __HAL_TIM_SET_AUTORELOAD(h,x) ((h)->Instance->ARR=(x))
#define __HAL_TIM_CLEAR_FLAG(h,x) ((h)->Instance->SR&=~(x))
#define __HAL_TIM_GET_FLAG(h,x) ((h)->Instance->SR&(x))
#define __HAL_TIM_DISABLE_IT(h,x) ((h)->Instance->DIER&=~(x))
#define __HAL_TIM_ENABLE_IT(h,x) ((h)->Instance->DIER|=(x))
static uint32_t primask;
static uint32_t __get_PRIMASK(void){return primask;}
static void __disable_irq(void){primask=1;}
static void __set_PRIMASK(uint32_t k){primask=k;}
static int fail_start,stops,posts;
static int st_err(int rc){return rc?MCS_HAL_ERR:0;}
static int HAL_TIM_Encoder_Start(TIM_HandleTypeDef* h,uint32_t channels){assert(channels==TIM_CHANNEL_ALL);h->Instance->CR1|=TIM_CR1_CEN;return fail_start;}
static int HAL_TIM_Encoder_Stop(TIM_HandleTypeDef* h,uint32_t channels){assert(channels==TIM_CHANNEL_ALL);h->Instance->CR1&=~TIM_CR1_CEN;stops++;return 0;}
static int HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef* h){(void)h;return 0;}
bool mcs_hal_post(int type,int source,int32_t value){(void)source;(void)value;assert(type==MCS_HAL_EV_TIMER);posts++;return true;}
'''
main=r'''
int main(void){
 TIM_TypeDef counter={0};counter.SMCR=TIM_ENCODERMODE_TI12;
 TIM_HandleTypeDef timer={&counter};board.encoder[0].h=&timer;board.encoder[0].a=0;board.encoder[0].b=1;
 void* h=NULL;uint32_t n;
 assert(st_encoder_open(&board,2,3,&h)==MCS_HAL_ENOTSUP && !h);
 counter.CR1=TIM_CR1_CEN;assert(st_encoder_open(&board,0,1,&h)==MCS_HAL_EBUSY && !h);counter.CR1=0;
 fail_start=1;assert(st_encoder_open(&board,0,1,&h)<0 && !h && !counter.CR1 && !board.encoder[0].active);
 fail_start=0;assert(st_encoder_open(&board,0,1,&h)==0 && h && counter.DIER==TIM_IT_UPDATE);
 assert(counter.ARR==65535 && counter.PSC==0 && st_encoder_busy(&board,&timer));
 void* other=NULL;assert(st_encoder_open(&board,0,1,&other)==MCS_HAL_EBUSY && !other);
 counter.CNT=120;assert(st_encoder_read(&board,h,&n)==0 && n==120);
 counter.CNT=3;counter.SR=1;primask=1;assert(st_encoder_read(&board,h,&n)==0 && n==65539 && primask==1 && !counter.SR);primask=0;
 counter.CNT=65534;counter.SR=1;assert(st_encoder_read(&board,h,&n)==0 && n==65534);
 counter.CNT=2;counter.SR=0;mcs_stm32_tim_elapsed(&timer);assert(st_encoder_read(&board,h,&n)==0 && n==65538 && !posts);
 counter.CNT=65535;mcs_stm32_tim_elapsed(&timer);assert(st_encoder_read(&board,h,&n)==0 && n==65535 && !posts);
 st_encoder_close(&board,h);assert(!counter.CR1 && !counter.DIER && !st_encoder_busy(&board,&timer));
 counter.wide=1;assert(st_encoder_open(&board,0,1,&h)==0 && !counter.DIER && counter.ARR==UINT32_MAX);
 counter.CNT=UINT32_MAX;assert(st_encoder_read(&board,h,&n)==0 && n==UINT32_MAX);
 counter.CNT=0;counter.SR=1;assert(st_encoder_read(&board,h,&n)==0 && n==0);
 st_encoder_close(&board,h);assert(stops==3);
 board.timer[0].h=&timer;mcs_stm32_tim_elapsed(&timer);assert(posts==1);
 puts("PASS STM32 TIM: native start/stop, 16/32-bit counts, overflow IRQ extension, pending-wrap snapshot, ownership and rollback");return 0;
}
'''
# Ensure every existing timer mutator excludes active native encoders.
for fn in ['st_pwm_set16','st_pwm_configure','st_pwm_stop','st_timer_start','st_timer_stop']:
 assert 'st_encoder_busy' in function('static int '+fn+'('),fn
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.c';p.write_text(prelude+backend+function('void mcs_stm32_tim_elapsed(')+main)
 exe=Path(tmp)/'test'
 subprocess.run([os.environ.get('CC','cc'),'-std=gnu99','-Wall','-Wextra','-Werror','-I'+str(root/'include'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
