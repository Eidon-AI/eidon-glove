#include "FingerTracking.h"
#include "HallEffectSensors.h"

int32_t angles[SENSOR_COUNT];
bool invertedSensors[SENSOR_COUNT] = {false}; // Default to no sensors inverted

void fingerTrackingSetup()
{
	// Default setup with no inverted sensors
	bool defaultInverted[SENSOR_COUNT] = {false};
	fingerTrackingSetup(defaultInverted);
}

void fingerTrackingSetup(const bool inverted[SENSOR_COUNT])
{
	hallEffectSensorsSetup();

	// Initialize angles array
	for (uint8_t i = 0; i < SENSOR_COUNT; i++)
	{
		angles[i] = 0;
		invertedSensors[i] = inverted[i]; // Store which sensors are inverted
	}
}

void setSensorInverted(uint8_t sensorIndex, bool inverted)
{
	if (sensorIndex < SENSOR_COUNT)
	{
		invertedSensors[sensorIndex] = inverted;
	}
}

void printFingerAngles()
{
	for (uint8_t i = 0; i < SENSOR_COUNT; i++)
	{
		Serial.print(">Joint_");
		Serial.print(i);
		Serial.print(":");
		Serial.print(angles[i]);
		Serial.print(" (");
		Serial.print(invertedSensors[i] ? "Inverted" : "Normal");
		Serial.println(")");
	}
}

void printRawAngles()
{
	for (uint8_t i = 0; i < SENSOR_COUNT; i++)
	{
		Serial.print(">Joint_");
		Serial.print(i);
		Serial.print(":");
		Serial.print(rawVals[i]);
		Serial.print(" (");
		Serial.print(invertedSensors[i] ? "Inverted" : "Normal");
		Serial.println(")");
	}
}

int32_t adjustMCPAbductionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * (2 * MCP_ABDUCTION_MAX));
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = (2 * MCP_ABDUCTION_MAX) - adjusted_angle;
	}
	
	return adjusted_angle;
}

int32_t adjustMCPFlexionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * MCP_FLEXION_MAX);
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = MCP_FLEXION_MAX - adjusted_angle;
	}
	
	return adjusted_angle;
}

int32_t adjustPIPFlexionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * PIP_FLEXION_MAX);
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = PIP_FLEXION_MAX - adjusted_angle;
	}
	
	return adjusted_angle;
}

int32_t adjustThumbCMCAbductionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * (2 * THUMB_CMC_ABDUCTION_MAX));
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = (2 * THUMB_CMC_ABDUCTION_MAX) - adjusted_angle;
	}
	
	return adjusted_angle;
}

int32_t adjustThumbCMCFlexionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * THUMB_CMC_FLEXION_MAX);
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = THUMB_CMC_FLEXION_MAX - adjusted_angle;
	}
	
	return adjusted_angle;
}

int32_t adjustThumbPIPFlexionAngle(int32_t i)
{
	float angle = proto_angles[i];
	float max_angle = max_angles[i];
	float min_angle = min_angles[i];
	int32_t adjusted_angle = (int32_t)((angle - min_angle) / (max_angle - min_angle) * THUMB_PIP_FLEXION_MAX);
	
	// Apply inversion if needed
	if (invertedSensors[i])
	{
		adjusted_angle = THUMB_PIP_FLEXION_MAX - adjusted_angle;
	}
	
	return adjusted_angle;
}

void adjustAngles()
{
	// thumb
	angles[0] = adjustThumbCMCFlexionAngle(0);
	angles[1] = adjustThumbCMCAbductionAngle(1);
	angles[2] = adjustThumbPIPFlexionAngle(2);
	angles[3] = proto_angles[3]; // not using this data currently

	// index
	angles[4] = adjustMCPAbductionAngle(4);
	angles[5] = adjustMCPFlexionAngle(5);
	angles[6] = adjustPIPFlexionAngle(6);

	// middle
	angles[7] = adjustMCPAbductionAngle(7);
	angles[8] = adjustMCPFlexionAngle(8);
	angles[9] = adjustPIPFlexionAngle(9);

	// ring
	angles[10] = adjustMCPAbductionAngle(10);
	angles[11] = adjustMCPFlexionAngle(11);
	angles[12] = adjustPIPFlexionAngle(12);

	// pinkie
	angles[13] = adjustMCPAbductionAngle(13);
	angles[14] = adjustMCPFlexionAngle(14);
	angles[15] = adjustPIPFlexionAngle(15);

	// // pinkie
	// angles[0] = adjustMCPAbductionAngle(0);
	// angles[1] = adjustMCPFlexionAngle(1);
	// angles[2] = adjustPIPFlexionAngle(2);

	// // ring
	// angles[3] = adjustMCPAbductionAngle(3);
	// angles[4] = adjustMCPFlexionAngle(4);
	// angles[5] = adjustPIPFlexionAngle(5);

	// // middle
	// angles[6] = adjustMCPAbductionAngle(6);
	// angles[7] = adjustMCPFlexionAngle(7);
	// angles[8] = adjustPIPFlexionAngle(8);

	// // index
	// angles[9] = adjustMCPAbductionAngle(9);
	// angles[10] = adjustMCPFlexionAngle(10);
	// angles[11] = adjustPIPFlexionAngle(11);

	// // TODO
	// // thumb
	// angles[12] = adjustThumbCMCFlexionAngle(12);
	// angles[13] = adjustThumbCMCAbductionAngle(13);
	// angles[14] = adjustThumbPIPFlexionAngle(14);
	// angles[15] = proto_angles[15]; // not using this data currently

	// //jank solution to inverted thumb movement on prototype glove
	// //TODO remove with glove v2
	// angles[12] = THUMB_CMC_FLEXION_MAX - angles[12];
	// angles[13] = THUMB_CMC_ABDUCTION_MAX*2 - angles[13];
	// angles[14] = THUMB_PIP_FLEXION_MAX - angles[14];


}

void calcFingerAngles()
{
	measureHallEffectSensors();
	calibrateHallEffectSensors();
	adjustAngles();
}
