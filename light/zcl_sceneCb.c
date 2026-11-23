/********************************************************************************************************
 * @file    zcl_sceneCb.c
 *
 * @brief   This is the source file for zcl_sceneCb
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


#if defined(ZCL_LIGHT_COLOR_CONTROL) && COLOR_CCT_SUPPORT
/*********************************************************************
 * @fn      tuyaLight_sceneColorTemperature
 *
 * @brief   Temperatura de color de un bloque de escena del cluster de color:
 *          - len >= 13: formato ZCL, ColorTemperatureMireds en los bytes 11-12.
 *          - len 2: formato de tuyaLight_sceneStoreReqHandler (solo mireds).
 *          - len >= 4: solo X/Y, como lo manda Zigbee2MQTT en scene_add;
 *            se pasa a mireds con la aproximacion de McCamy.
 *
 * @param   pData	-	datos del bloque
 * @param   len		-	longitud de los datos
 *
 * @return  mireds, o 0 si el bloque no trae una temperatura valida
 */
static u16 tuyaLight_sceneColorTemperature(const u8 *pData, u8 len)
{
	if(len >= 13){
		u16 mireds = BUILD_U16(pData[11], pData[12]);
		if(mireds){
			return mireds;
		}
	}else if(len == 2){
		return BUILD_U16(pData[0], pData[1]);
	}

	if(len < 4){
		return 0;
	}

	u16 rawX = BUILD_U16(pData[0], pData[1]);
	u16 rawY = BUILD_U16(pData[2], pData[3]);
	if((rawX == 0) && (rawY == 0)){
		return 0;
	}

	// sin coma flotante en el toolchain: x, y en diezmilesimas y n en milesimas
	s32 x = ((s32)rawX * 10000) / 65535;
	s32 y = ((s32)rawY * 10000) / 65535;
	s32 den = 1858 - y;
	if(den == 0){
		return 0;
	}
	s32 n = ((x - 3320) * 1000) / den;
	if((n < -2000) || (n > 2000)){
		return 0;
	}

	s32 kelvin = (437 * n) / 1000 + 3601;
	kelvin = (kelvin * n) / 1000 + 6861;
	kelvin = (kelvin * n) / 1000 + 5517;
	if((kelvin <= 1000) || (kelvin >= 20000)){
		return 0;
	}

	return (u16)(1000000 / kelvin);
}
#endif

/*********************************************************************
 * @fn      tuyaLight_sceneRecallReqHandler
 *
 * @brief   Aplica una escena. El campo de extension es una lista de bloques
 *          {clusterId (2), len (1), datos (len)} en cualquier orden; solo se
 *          aplica lo que trae la escena.
 *
 * @param	pAddrInfo
 * @param	pScene
 *
 * @return  None
 */
static void tuyaLight_sceneRecallReqHandler(zclIncomingAddrInfo_t *pAddrInfo, zcl_sceneEntry_t *pScene)
{
	// la escena guarda segundos + decimas; las ordenes de nivel y color usan decimas
	u32 transitionTime = (u32)pScene->transTime * 10 + pScene->transTime100ms;
	if(transitionTime > 0xFFFE){
		transitionTime = 0xFFFE;
	}

	const u8 *pField = pScene->extField;
	const u8 *pEnd = pScene->extField + pScene->extFieldLen;

	while(pField + 3 <= pEnd){
		u16 clusterId = BUILD_U16(pField[0], pField[1]);
		u8 len = pField[2];
		const u8 *pData = pField + 3;

		if(pData + len > pEnd){
			break;
		}

		switch(clusterId){
#ifdef ZCL_ON_OFF
			case ZCL_CLUSTER_GEN_ON_OFF:
				if(len >= 1){
					tuyaLight_onoff(pData[0] ? ZCL_CMD_ONOFF_ON : ZCL_CMD_ONOFF_OFF);
				}
				break;
#endif
#ifdef ZCL_LEVEL_CTRL
			case ZCL_CLUSTER_GEN_LEVEL_CONTROL:
				if(len >= 1){
					moveToLvl_t moveToLevel;
					moveToLevel.level = pData[0];
					moveToLevel.transitionTime = (u16)transitionTime;
					moveToLevel.optPresent = 0;

					tuyaLight_levelCb(pAddrInfo, ZCL_CMD_LEVEL_MOVE_TO_LEVEL, &moveToLevel);
				}
				break;
#endif
#if defined(ZCL_LIGHT_COLOR_CONTROL) && COLOR_CCT_SUPPORT
			case ZCL_CLUSTER_LIGHTING_COLOR_CONTROL:
			{
				u16 mireds = tuyaLight_sceneColorTemperature(pData, len);
				if(mireds){
					zcl_colorCtrlMoveToColorTemperatureCmd_t move2ColorTemp;
					move2ColorTemp.colorTemperature = mireds;
					move2ColorTemp.transitionTime = (u16)transitionTime;
					move2ColorTemp.optPresent = 0;

					tuyaLight_colorCtrlCb(pAddrInfo, ZCL_CMD_LIGHT_COLOR_CONTROL_MOVE_TO_COLOR_TEMPERATURE, &move2ColorTemp);
				}
				break;
			}
#endif
			default:
				break;
		}

		pField = pData + len;
	}
}

/*********************************************************************
 * @fn      tuyaLight_sceneStoreReqHandler
 *
 * @brief   Handler for ZCL scene store command. This function will set scene attribute first.
 *
 * @param   pApsdeInd
 * @param   pScene
 *
 * @return  None
 */
static void tuyaLight_sceneStoreReqHandler(zcl_sceneEntry_t *pScene)
{
	/* receive Store Scene Request command, get the latest Scene info to save */
	u8 extLen = 0;

#ifdef ZCL_ON_OFF
	zcl_onOffAttr_t *pOnOff = zcl_onoffAttrGet();

	pScene->extField[extLen++] = LO_UINT16(ZCL_CLUSTER_GEN_ON_OFF);
	pScene->extField[extLen++] = HI_UINT16(ZCL_CLUSTER_GEN_ON_OFF);
	pScene->extField[extLen++] = 1;
	pScene->extField[extLen++] = pOnOff->onOff;
#endif

#ifdef ZCL_LEVEL_CTRL
	zcl_levelAttr_t *pLevel = zcl_levelAttrGet();

	pScene->extField[extLen++] = LO_UINT16(ZCL_CLUSTER_GEN_LEVEL_CONTROL);
	pScene->extField[extLen++] = HI_UINT16(ZCL_CLUSTER_GEN_LEVEL_CONTROL);
	pScene->extField[extLen++] = 1;
	pScene->extField[extLen++] = pLevel->curLevel;
#endif

#ifdef ZCL_LIGHT_COLOR_CONTROL
	zcl_lightColorCtrlAttr_t *pColor = zcl_colorAttrGet();

	pScene->extField[extLen++] = LO_UINT16(ZCL_CLUSTER_LIGHTING_COLOR_CONTROL);
	pScene->extField[extLen++] = HI_UINT16(ZCL_CLUSTER_LIGHTING_COLOR_CONTROL);
#if COLOR_RGB_SUPPORT
	pScene->extField[extLen++] = 2;
	pScene->extField[extLen++] = pColor->currentHue;
	pScene->extField[extLen++] = pColor->currentSaturation;
#elif COLOR_CCT_SUPPORT
	pScene->extField[extLen++] = 2;
	pScene->extField[extLen++] = LO_UINT16(pColor->colorTemperatureMireds);
	pScene->extField[extLen++] = HI_UINT16(pColor->colorTemperatureMireds);
#endif
#endif

	pScene->extFieldLen = extLen;
}

/*********************************************************************
 * @fn      tuyaLight_sceneCb
 *
 * @brief   Handler for ZCL Scene command.
 *
 * @param   pApsdeInd
 *
 * @return  None
 */
status_t tuyaLight_sceneCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload)
{
	if(pAddrInfo->dstEp == TUYA_LIGHT_ENDPOINT){
		if(pAddrInfo->dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR){
			switch(cmdId){
				case ZCL_CMD_SCENE_STORE_SCENE:
					tuyaLight_sceneStoreReqHandler((zcl_sceneEntry_t *)cmdPayload);
					break;
				case ZCL_CMD_SCENE_RECALL_SCENE:
					tuyaLight_sceneRecallReqHandler(pAddrInfo, (zcl_sceneEntry_t *)cmdPayload);
					break;
				default:
					break;
			}
		}
	}

	return ZCL_STA_SUCCESS;
}

#endif  /* __PROJECT_TL_DIMMABLE_LIGHT__ */
