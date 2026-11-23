/********************************************************************************************************
 * @file    zcl_levelCb.c
 *
 * @brief   This is the source file for zcl_levelCb
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *			All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/

#if (__PROJECT_TL_DIMMABLE_LIGHT__)
/**********************************************************************
 * INCLUDES
 */
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "tuyaLight.h"
#include "tuyaLightCtrl.h"

#ifdef ZCL_LEVEL_CTRL

/**********************************************************************
 * LOCAL CONSTANTS
 */
#define ZCL_LEVEL_CHANGE_INTERVAL		100

/**********************************************************************
 * TYPEDEFS
 */
typedef struct{
	s32 stepLevel256;
	u16	currentLevel256;
	u8	targetLevel;
	bool offAtMin;
}zcl_levelInfo_t;

/**********************************************************************
 * LOCAL VARIABLES
 */
static zcl_levelInfo_t levelInfo = {
	.stepLevel256 		= 0,
	.currentLevel256	= 0,
	.targetLevel		= 0,
	.offAtMin			= FALSE,
};

static ev_timer_event_t *levelTimerEvt = NULL;

/**********************************************************************
 * FUNCTIONS
 */

/*********************************************************************
 * @fn      tuyaLight_levelInit
 *
 * @brief
 *
 * @param   None
 *
 * @return  None
 */
void tuyaLight_levelInit(void)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	pLevel->remainingTime = 0;

	levelInfo.currentLevel256 = (u16)(pLevel->curLevel) << 8;

	light_applyUpdate(&pLevel->curLevel, &levelInfo.currentLevel256, &levelInfo.stepLevel256, &pLevel->remainingTime,
					ZCL_LEVEL_ATTR_MIN_LEVEL, ZCL_LEVEL_ATTR_MAX_LEVEL, FALSE);
}

/*********************************************************************
 * @fn      tuyaLight_updateLevel
 *
 * @brief
 *
 * @param   None
 *
 * @return  None
 */
void tuyaLight_updateLevel(void)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	hwLight_levelUpdate(pLevel->curLevel);
}

/*********************************************************************
 * @fn      tuyaLight_levelStep
 *
 * @brief   Avanza un paso de la transicion de nivel. En el ultimo paso fija
 *          el nivel destino exacto y, si la orden era "with On/Off" hacia el
 *          minimo, apaga la luz.
 *
 * @param   None
 *
 * @return  None
 */
static void tuyaLight_levelStep(void)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	// el paso se trunca al dividir; el ultimo recorre lo que falte
	if(pLevel->remainingTime == 1){
		levelInfo.stepLevel256 = ((s32)levelInfo.targetLevel << 8) - (s32)levelInfo.currentLevel256;
	}

	light_applyUpdate(&pLevel->curLevel, &levelInfo.currentLevel256, &levelInfo.stepLevel256, &pLevel->remainingTime,
							ZCL_LEVEL_ATTR_MIN_LEVEL, ZCL_LEVEL_ATTR_MAX_LEVEL, FALSE);

	if((pLevel->remainingTime == 0) && levelInfo.offAtMin){
		levelInfo.offAtMin = FALSE;
		tuyaLight_onoff(ZCL_CMD_ONOFF_OFF);
	}
}

/*********************************************************************
 * @fn      tuyaLight_levelTimerEvtCb
 *
 * @brief   timer event to process the level command
 *
 * @param	arg
 *
 * @return  0: timer continue on; -1: timer will be canceled
 */
static s32 tuyaLight_levelTimerEvtCb(void * arg)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	if(pLevel->remainingTime){
		tuyaLight_levelStep();
	}

	if(pLevel->remainingTime){
		return 0;
	}else{
		levelTimerEvt = NULL;
		return -1;
	}
}

/*********************************************************************
 * @fn      tuyaLight_LevelTimerStop
 *
 * @brief   force to stop the level timer
 *
 * @param
 *
 * @return
 */
static void tuyaLight_LevelTimerStop(void)
{
	if(levelTimerEvt){
		TL_ZB_TIMER_CANCEL(&levelTimerEvt);
	}
}

/*********************************************************************
 * @fn      tuyaLight_levelTransitionStart
 *
 * @brief   Inicia una transicion de nivel hacia targetLevel en remainingTime
 *          pasos de 100 ms. Con "with On/Off" enciende si el destino supera
 *          el minimo y apaga al terminar si el destino es el minimo.
 *
 * @param   withOnOff		-	orden "with On/Off"
 * @param   targetLevel		-	nivel destino, se limita a [min, max]
 * @param   remainingTime	-	pasos de 100 ms, minimo 1
 *
 * @return  None
 */
static void tuyaLight_levelTransitionStart(bool withOnOff, s32 targetLevel, u16 remainingTime)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	if(targetLevel < ZCL_LEVEL_ATTR_MIN_LEVEL){
		targetLevel = ZCL_LEVEL_ATTR_MIN_LEVEL;
	}else if(targetLevel > ZCL_LEVEL_ATTR_MAX_LEVEL){
		targetLevel = ZCL_LEVEL_ATTR_MAX_LEVEL;
	}

	tuyaLight_LevelTimerStop();

	pLevel->remainingTime = remainingTime;
	levelInfo.targetLevel = (u8)targetLevel;
	levelInfo.offAtMin = withOnOff && (targetLevel == ZCL_LEVEL_ATTR_MIN_LEVEL);
	levelInfo.currentLevel256 = (u16)(pLevel->curLevel) << 8;
	levelInfo.stepLevel256 = ((targetLevel - (s32)pLevel->curLevel) << 8) / (s32)remainingTime;

	if(withOnOff && (targetLevel > ZCL_LEVEL_ATTR_MIN_LEVEL)){
		tuyaLight_onoff(ZCL_CMD_ONOFF_ON);
	}

	tuyaLight_levelStep();

	if(pLevel->remainingTime){
		levelTimerEvt = TL_ZB_TIMER_SCHEDULE(tuyaLight_levelTimerEvtCb, NULL, ZCL_LEVEL_CHANGE_INTERVAL);
	}
}

/*********************************************************************
 * @fn      tuyaLight_levelStopFadeToOff
 *
 * @brief   Detiene una transicion que iba a terminar apagando la luz, para
 *          que una orden On/Off posterior no quede anulada al terminar.
 *
 * @param   None
 *
 * @return  None
 */
void tuyaLight_levelStopFadeToOff(void)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	if(levelInfo.offAtMin){
		tuyaLight_LevelTimerStop();
		pLevel->remainingTime = 0;
		levelInfo.offAtMin = FALSE;
		levelInfo.currentLevel256 = (u16)(pLevel->curLevel) << 8;
	}
}

static u16 tuyaLight_transitionSteps(u16 transitionTime)
{
	return ((transitionTime == 0) || (transitionTime == 0xFFFF)) ? 1 : transitionTime;
}

/*********************************************************************
 * @fn      tuyaLight_moveToLevelProcess
 *
 * @brief
 *
 * @param	cmdId
 * @param	cmd
 *
 * @return	None
 */
static void tuyaLight_moveToLevelProcess(u8 cmdId, moveToLvl_t *cmd)
{
	tuyaLight_levelTransitionStart(cmdId == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
								   cmd->level, tuyaLight_transitionSteps(cmd->transitionTime));
}

/*********************************************************************
 * @fn      tuyaLight_moveProcess
 *
 * @brief
 *
 * @param	cmdId
 * @param	cmd
 *
 * @return	None
 */
static void tuyaLight_moveProcess(u8 cmdId, move_t *cmd)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	if(cmd->rate == 0){
		return;
	}

	u8 targetLevel;
	u8 deltaLevel;
	if(cmd->moveMode == LEVEL_MOVE_UP){
		targetLevel = ZCL_LEVEL_ATTR_MAX_LEVEL;
		deltaLevel = ZCL_LEVEL_ATTR_MAX_LEVEL - pLevel->curLevel;
	}else{
		targetLevel = ZCL_LEVEL_ATTR_MIN_LEVEL;
		deltaLevel = pLevel->curLevel - ZCL_LEVEL_ATTR_MIN_LEVEL;
	}

	// rate en niveles/s; remainingTime en pasos de 100 ms
	u16 remainingTime = ((u32)deltaLevel * 10) / cmd->rate;
	if(remainingTime == 0){
		remainingTime = 1;
	}

	tuyaLight_levelTransitionStart(cmdId == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, targetLevel, remainingTime);
}

/*********************************************************************
 * @fn      tuyaLight_stepProcess
 *
 * @brief
 *
 * @param	cmdId
 * @param	cmd
 *
 * @return	None
 */
static void tuyaLight_stepProcess(u8 cmdId, step_t *cmd)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	s32 targetLevel = (cmd->stepMode == LEVEL_STEP_UP) ? ((s32)pLevel->curLevel + cmd->stepSize)
													   : ((s32)pLevel->curLevel - cmd->stepSize);

	tuyaLight_levelTransitionStart(cmdId == ZCL_CMD_LEVEL_STEP_WITH_ON_OFF,
								   targetLevel, tuyaLight_transitionSteps(cmd->transitionTime));
}

/*********************************************************************
 * @fn      tuyaLight_stopProcess
 *
 * @brief
 *
 * @param	cmdId
 * @param	cmd
 *
 * @return	None
 */
static void tuyaLight_stopProcess(u8 cmdId, stop_t *cmd)
{
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	tuyaLight_LevelTimerStop();
	pLevel->remainingTime = 0;
	levelInfo.offAtMin = FALSE;

	levelInfo.currentLevel256 = (u16)(pLevel->curLevel) << 8;
}

/*********************************************************************
 * @fn      tuyaLight_levelCb
 *
 * @brief   Handler for ZCL LEVEL command. This function will set LEVEL attribute first.
 *
 * @param	pAddrInfo
 * @param   cmd - level cluster command id
 * @param   cmdPayload
 *
 * @return  status_t
 */
status_t tuyaLight_levelCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload)
{
	if(pAddrInfo->dstEp == TUYA_LIGHT_ENDPOINT){
		switch(cmdId){
			case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
			case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
				tuyaLight_moveToLevelProcess(cmdId, (moveToLvl_t *)cmdPayload);
				break;
			case ZCL_CMD_LEVEL_MOVE:
			case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF:
				tuyaLight_moveProcess(cmdId, (move_t *)cmdPayload);
				break;
			case ZCL_CMD_LEVEL_STEP:
			case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF:
				tuyaLight_stepProcess(cmdId, (step_t *)cmdPayload);
				break;
			case ZCL_CMD_LEVEL_STOP:
			case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
				tuyaLight_stopProcess(cmdId, (stop_t *)cmdPayload);
				break;
			default:
				break;
		}
	}

	return ZCL_STA_SUCCESS;
}

#endif	/* ZCL_LEVEL_CTRL */

#endif  /* __PROJECT_TL_DIMMABLE_LIGHT__ */
