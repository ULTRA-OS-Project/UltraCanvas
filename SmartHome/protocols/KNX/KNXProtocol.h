// KNXProtocol.h
// KNX Protocol Implementation for UltraCanvas SmartHome
// Version: 1.0.0
// Last Modified: 2025-12-09
// Author: UltraCanvas Framework

#pragma once

#include "ISmartHomeProtocol.h"
#include "SmartHomeProtocolBase.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <array>

namespace UltraCanvas {
namespace SmartHome {

// ============================================================================
// KNX ADDRESS TYPES
// ============================================================================

/**
 * @brief KNX Individual/Physical Address (Area.Line.Device)
 * Format: 4 bits Area (0-15), 4 bits Line (0-15), 8 bits Device (0-255)
 * Example: 1.2.3 = Area 1, Line 2, Device 3
 */
struct KNXIndividualAddress {
    uint8_t Area = 0;      ///< 0-15
    uint8_t Line = 0;      ///< 0-15
    uint8_t Device = 0;    ///< 0-255
    
    KNXIndividualAddress() = default;
    KNXIndividualAddress(uint8_t a, uint8_t l, uint8_t d) : Area(a), Line(l), Device(d) {}
    KNXIndividualAddress(uint16_t raw) {
        Area = (raw >> 12) & 0x0F;
        Line = (raw >> 8) & 0x0F;
        Device = raw & 0xFF;
    }
    
    uint16_t ToRaw() const {
        return ((Area & 0x0F) << 12) | ((Line & 0x0F) << 8) | Device;
    }
    
    std::string ToString() const {
        return std::to_string(Area) + "." + std::to_string(Line) + "." + std::to_string(Device);
    }
    
    static KNXIndividualAddress FromString(const std::string& str);
    
    bool operator==(const KNXIndividualAddress& other) const {
        return Area == other.Area && Line == other.Line && Device == other.Device;
    }
    
    bool operator<(const KNXIndividualAddress& other) const {
        return ToRaw() < other.ToRaw();
    }
};

/**
 * @brief KNX Group Address
 * 3-level format: Main/Middle/Sub (5/3/8 bits) - 0-31/0-7/0-255
 * 2-level format: Main/Sub (5/11 bits) - 0-31/0-2047
 * Free format: 16-bit value
 * Example: 1/2/3 = Main 1, Middle 2, Sub 3
 */
struct KNXGroupAddress {
    uint16_t Raw = 0;
    
    // 3-level components
    uint8_t Main = 0;      ///< 0-31 (5 bits)
    uint8_t Middle = 0;    ///< 0-7 (3 bits)
    uint8_t Sub = 0;       ///< 0-255 (8 bits)
    
    KNXGroupAddress() = default;
    KNXGroupAddress(uint16_t raw) : Raw(raw) {
        Main = (raw >> 11) & 0x1F;
        Middle = (raw >> 8) & 0x07;
        Sub = raw & 0xFF;
    }
    KNXGroupAddress(uint8_t main, uint8_t middle, uint8_t sub)
        : Main(main), Middle(middle), Sub(sub) {
        Raw = ((main & 0x1F) << 11) | ((middle & 0x07) << 8) | sub;
    }
    
    std::string ToString() const {
        return std::to_string(Main) + "/" + std::to_string(Middle) + "/" + std::to_string(Sub);
    }
    
    std::string ToString2Level() const {
        uint16_t sub2 = ((Middle & 0x07) << 8) | Sub;
        return std::to_string(Main) + "/" + std::to_string(sub2);
    }
    
    static KNXGroupAddress FromString(const std::string& str);
    
    bool operator==(const KNXGroupAddress& other) const { return Raw == other.Raw; }
    bool operator<(const KNXGroupAddress& other) const { return Raw < other.Raw; }
};

// ============================================================================
// KNX DATAPOINT TYPES (DPT)
// ============================================================================

/**
 * @brief KNX Datapoint Type categories
 */
enum class KNXDatapointType : uint16_t {
    // DPT 1 - 1 bit
    DPT_Switch = 0x0001,           ///< 1.001 - Switch (on/off)
    DPT_Bool = 0x0002,             ///< 1.002 - Boolean
    DPT_Enable = 0x0003,           ///< 1.003 - Enable
    DPT_Ramp = 0x0004,             ///< 1.004 - Ramp
    DPT_Alarm = 0x0005,            ///< 1.005 - Alarm
    DPT_BinaryValue = 0x0006,      ///< 1.006 - Binary value
    DPT_Step = 0x0007,             ///< 1.007 - Step
    DPT_UpDown = 0x0008,           ///< 1.008 - Up/Down
    DPT_OpenClose = 0x0009,        ///< 1.009 - Open/Close
    DPT_Start = 0x000A,            ///< 1.010 - Start
    DPT_State = 0x000B,            ///< 1.011 - State
    DPT_Invert = 0x000C,           ///< 1.012 - Invert
    DPT_DimSendStyle = 0x000D,     ///< 1.013 - Dim send style
    DPT_InputSource = 0x000E,      ///< 1.014 - Input source
    DPT_Reset = 0x000F,            ///< 1.015 - Reset
    DPT_Ack = 0x0010,              ///< 1.016 - Acknowledge
    DPT_Trigger = 0x0011,          ///< 1.017 - Trigger
    DPT_Occupancy = 0x0012,        ///< 1.018 - Occupancy
    DPT_WindowDoor = 0x0013,       ///< 1.019 - Window/Door
    
    // DPT 2 - 1 bit controlled
    DPT_Switch_Control = 0x0200,   ///< 2.001 - Switch controlled
    DPT_Bool_Control = 0x0201,     ///< 2.002 - Boolean controlled
    DPT_Enable_Control = 0x0202,   ///< 2.003 - Enable controlled
    
    // DPT 3 - 3 bit controlled
    DPT_Control_Dimming = 0x0307,  ///< 3.007 - Control dimming
    DPT_Control_Blinds = 0x0308,   ///< 3.008 - Control blinds
    
    // DPT 4 - Character
    DPT_Char_ASCII = 0x0401,       ///< 4.001 - Character ASCII
    DPT_Char_8859_1 = 0x0402,      ///< 4.002 - Character ISO 8859-1
    
    // DPT 5 - 8-bit unsigned
    DPT_Scaling = 0x0501,          ///< 5.001 - Scaling (0-100%)
    DPT_Angle = 0x0503,            ///< 5.003 - Angle (0-360°)
    DPT_Percent_U8 = 0x0504,       ///< 5.004 - Percent (0-255)
    DPT_DecimalFactor = 0x0505,    ///< 5.005 - Decimal factor
    DPT_Tariff = 0x0506,           ///< 5.006 - Tariff
    DPT_Value_1_Ucount = 0x050A,   ///< 5.010 - Counter (0-255)
    
    // DPT 6 - 8-bit signed
    DPT_Percent_V8 = 0x0601,       ///< 6.001 - Percent (-128..127)
    DPT_Value_1_Count = 0x060A,    ///< 6.010 - Counter (-128..127)
    DPT_StatusMode3 = 0x0614,      ///< 6.020 - Status mode 3
    
    // DPT 7 - 2-byte unsigned
    DPT_Value_2_Ucount = 0x0701,   ///< 7.001 - Counter (0-65535)
    DPT_TimePeriodMsec = 0x0702,   ///< 7.002 - Time period msec
    DPT_TimePeriod10Msec = 0x0703, ///< 7.003 - Time period 10msec
    DPT_TimePeriod100Msec = 0x0704,///< 7.004 - Time period 100msec
    DPT_TimePeriodSec = 0x0705,    ///< 7.005 - Time period sec
    DPT_TimePeriodMin = 0x0706,    ///< 7.006 - Time period min
    DPT_TimePeriodHrs = 0x0707,    ///< 7.007 - Time period hours
    DPT_PropDataType = 0x070A,     ///< 7.010 - Property data type
    DPT_Length_mm = 0x0711,        ///< 7.011 - Length mm
    DPT_UElCurrentmA = 0x0712,     ///< 7.012 - Current mA
    DPT_Brightness = 0x0713,       ///< 7.013 - Brightness lux
    
    // DPT 8 - 2-byte signed
    DPT_Value_2_Count = 0x0801,    ///< 8.001 - Counter (-32768..32767)
    DPT_DeltaTimeMsec = 0x0802,    ///< 8.002 - Delta time msec
    DPT_DeltaTime10Msec = 0x0803,  ///< 8.003 - Delta time 10msec
    DPT_DeltaTime100Msec = 0x0804, ///< 8.004 - Delta time 100msec
    DPT_DeltaTimeSec = 0x0805,     ///< 8.005 - Delta time sec
    DPT_DeltaTimeMin = 0x0806,     ///< 8.006 - Delta time min
    DPT_DeltaTimeHrs = 0x0807,     ///< 8.007 - Delta time hours
    DPT_Percent_V16 = 0x080A,      ///< 8.010 - Percent
    DPT_Rotation_Angle = 0x080B,   ///< 8.011 - Rotation angle
    
    // DPT 9 - 2-byte float
    DPT_Value_Temp = 0x0901,       ///< 9.001 - Temperature °C
    DPT_Value_Tempd = 0x0902,      ///< 9.002 - Temperature difference K
    DPT_Value_Tempa = 0x0903,      ///< 9.003 - Temperature gradient K/h
    DPT_Value_Lux = 0x0904,        ///< 9.004 - Luminous intensity lux
    DPT_Value_Wsp = 0x0905,        ///< 9.005 - Wind speed m/s
    DPT_Value_Pres = 0x0906,       ///< 9.006 - Pressure Pa
    DPT_Value_Humidity = 0x0907,   ///< 9.007 - Humidity %
    DPT_Value_AirQuality = 0x0908, ///< 9.008 - Air quality ppm
    DPT_Value_AirFlow = 0x0909,    ///< 9.009 - Air flow m³/h
    DPT_Value_Time1 = 0x090A,      ///< 9.010 - Time s
    DPT_Value_Time2 = 0x090B,      ///< 9.011 - Time ms
    DPT_Value_Volt = 0x0914,       ///< 9.020 - Voltage mV
    DPT_Value_Curr = 0x0915,       ///< 9.021 - Current mA
    DPT_PowerDensity = 0x0916,     ///< 9.022 - Power density W/m²
    DPT_KelvinPerPercent = 0x0917, ///< 9.023 - Kelvin/percent K/%
    DPT_Power = 0x0918,            ///< 9.024 - Power kW
    DPT_Value_Volume_Flow = 0x0919,///< 9.025 - Volume flow l/h
    DPT_Rain_Amount = 0x091A,      ///< 9.026 - Rain amount l/m²
    DPT_Value_Temp_F = 0x091B,     ///< 9.027 - Temperature °F
    DPT_Value_Wsp_kmh = 0x091C,    ///< 9.028 - Wind speed km/h
    
    // DPT 10 - Time
    DPT_TimeOfDay = 0x0A01,        ///< 10.001 - Time of day
    
    // DPT 11 - Date
    DPT_Date = 0x0B01,             ///< 11.001 - Date
    
    // DPT 12 - 4-byte unsigned
    DPT_Value_4_Ucount = 0x0C01,   ///< 12.001 - Counter (0-4294967295)
    DPT_Volume_Liquid_Litre = 0x0C64, ///< 12.100 - Volume liquid l
    DPT_Volume_m3 = 0x0C65,        ///< 12.101 - Volume m³
    
    // DPT 13 - 4-byte signed
    DPT_Value_4_Count = 0x0D01,    ///< 13.001 - Counter
    DPT_FlowRate_m3h = 0x0D02,     ///< 13.002 - Flow rate m³/h
    DPT_ActiveEnergy = 0x0D0A,     ///< 13.010 - Active energy Wh
    DPT_ApparantEnergy = 0x0D0B,   ///< 13.011 - Apparent energy VAh
    DPT_ReactiveEnergy = 0x0D0C,   ///< 13.012 - Reactive energy VARh
    DPT_ActiveEnergy_kWh = 0x0D0D, ///< 13.013 - Active energy kWh
    DPT_ApparantEnergy_kVAh = 0x0D0E, ///< 13.014 - Apparent energy kVAh
    DPT_ReactiveEnergy_kVARh = 0x0D0F, ///< 13.015 - Reactive energy kVARh
    DPT_LongDeltaTimeSec = 0x0D64, ///< 13.100 - Delta time s
    
    // DPT 14 - 4-byte float
    DPT_Value_Acceleration = 0x0E00,     ///< 14.000 - Acceleration m/s²
    DPT_Value_Acceleration_Angular = 0x0E01, ///< 14.001 - Angular acceleration rad/s²
    DPT_Value_Activation_Energy = 0x0E02,///< 14.002 - Activation energy J/mol
    DPT_Value_Activity = 0x0E03,         ///< 14.003 - Activity 1/s
    DPT_Value_Mol = 0x0E04,              ///< 14.004 - Amount of substance mol
    DPT_Value_Amplitude = 0x0E05,        ///< 14.005 - Amplitude
    DPT_Value_AngleRad = 0x0E06,         ///< 14.006 - Angle rad
    DPT_Value_AngleDeg = 0x0E07,         ///< 14.007 - Angle °
    DPT_Value_Angular_Momentum = 0x0E08, ///< 14.008 - Angular momentum Js
    DPT_Value_Angular_Velocity = 0x0E09, ///< 14.009 - Angular velocity rad/s
    DPT_Value_Area = 0x0E0A,             ///< 14.010 - Area m²
    DPT_Value_Capacitance = 0x0E0B,      ///< 14.011 - Capacitance F
    DPT_Value_Charge_DensitySurface = 0x0E0C, ///< 14.012 - Charge density surface C/m²
    DPT_Value_Charge_DensityVolume = 0x0E0D,  ///< 14.013 - Charge density volume C/m³
    DPT_Value_Compressibility = 0x0E0E,  ///< 14.014 - Compressibility m²/N
    DPT_Value_Conductance = 0x0E0F,      ///< 14.015 - Conductance S
    DPT_Value_Electrical_Conductivity = 0x0E10, ///< 14.016 - Electrical conductivity S/m
    DPT_Value_Density = 0x0E11,          ///< 14.017 - Density kg/m³
    DPT_Value_Electric_Charge = 0x0E12,  ///< 14.018 - Electric charge C
    DPT_Value_Electric_Current = 0x0E13, ///< 14.019 - Electric current A
    DPT_Value_Electric_CurrentDensity = 0x0E14, ///< 14.020 - Current density A/m²
    DPT_Value_Electric_DipoleMoment = 0x0E15,   ///< 14.021 - Electric dipole moment Cm
    DPT_Value_Electric_Displacement = 0x0E16,   ///< 14.022 - Electric displacement C/m²
    DPT_Value_Electric_FieldStrength = 0x0E17,  ///< 14.023 - Electric field strength V/m
    DPT_Value_Electric_Flux = 0x0E18,    ///< 14.024 - Electric flux C
    DPT_Value_Electric_FluxDensity = 0x0E19,    ///< 14.025 - Electric flux density C/m²
    DPT_Value_Electric_Polarization = 0x0E1A,   ///< 14.026 - Electric polarization C/m²
    DPT_Value_Electric_Potential = 0x0E1B,      ///< 14.027 - Electric potential V
    DPT_Value_Electric_PotentialDifference = 0x0E1C, ///< 14.028 - Voltage V
    DPT_Value_ElectromagneticMoment = 0x0E1D,   ///< 14.029 - Electromagnetic moment Am²
    DPT_Value_Electromotive_Force = 0x0E1E,     ///< 14.030 - Electromotive force V
    DPT_Value_Energy = 0x0E1F,           ///< 14.031 - Energy J
    DPT_Value_Force = 0x0E20,            ///< 14.032 - Force N
    DPT_Value_Frequency = 0x0E21,        ///< 14.033 - Frequency Hz
    DPT_Value_Angular_Frequency = 0x0E22,///< 14.034 - Angular frequency rad/s
    DPT_Value_Heat_Capacity = 0x0E23,    ///< 14.035 - Heat capacity J/K
    DPT_Value_Heat_FlowRate = 0x0E24,    ///< 14.036 - Heat flow rate W
    DPT_Value_Heat_Quantity = 0x0E25,    ///< 14.037 - Heat quantity J
    DPT_Value_Impedance = 0x0E26,        ///< 14.038 - Impedance Ω
    DPT_Value_Length = 0x0E27,           ///< 14.039 - Length m
    DPT_Value_Light_Quantity = 0x0E28,   ///< 14.040 - Light quantity lm·s
    DPT_Value_Luminance = 0x0E29,        ///< 14.041 - Luminance cd/m²
    DPT_Value_Luminous_Flux = 0x0E2A,    ///< 14.042 - Luminous flux lm
    DPT_Value_Luminous_Intensity = 0x0E2B, ///< 14.043 - Luminous intensity cd
    DPT_Value_Magnetic_FieldStrength = 0x0E2C, ///< 14.044 - Magnetic field strength A/m
    DPT_Value_Magnetic_Flux = 0x0E2D,    ///< 14.045 - Magnetic flux Wb
    DPT_Value_Magnetic_FluxDensity = 0x0E2E,   ///< 14.046 - Magnetic flux density T
    DPT_Value_Magnetic_Moment = 0x0E2F,  ///< 14.047 - Magnetic moment Am²
    DPT_Value_Magnetic_Polarization = 0x0E30,  ///< 14.048 - Magnetic polarization T
    DPT_Value_Magnetization = 0x0E31,    ///< 14.049 - Magnetization A/m
    DPT_Value_MagnetomotiveForce = 0x0E32,     ///< 14.050 - Magnetomotive force A
    DPT_Value_Mass = 0x0E33,             ///< 14.051 - Mass kg
    DPT_Value_MassFlux = 0x0E34,         ///< 14.052 - Mass flux kg/s
    DPT_Value_Momentum = 0x0E35,         ///< 14.053 - Momentum N·s
    DPT_Value_Phase_AngleRad = 0x0E36,   ///< 14.054 - Phase angle rad
    DPT_Value_Phase_AngleDeg = 0x0E37,   ///< 14.055 - Phase angle °
    DPT_Value_Power = 0x0E38,            ///< 14.056 - Power W
    DPT_Value_Power_Factor = 0x0E39,     ///< 14.057 - Power factor
    DPT_Value_Pressure = 0x0E3A,         ///< 14.058 - Pressure Pa
    DPT_Value_Reactance = 0x0E3B,        ///< 14.059 - Reactance Ω
    DPT_Value_Resistance = 0x0E3C,       ///< 14.060 - Resistance Ω
    DPT_Value_Resistivity = 0x0E3D,      ///< 14.061 - Resistivity Ω·m
    DPT_Value_SelfInductance = 0x0E3E,   ///< 14.062 - Self inductance H
    DPT_Value_SolidAngle = 0x0E3F,       ///< 14.063 - Solid angle sr
    DPT_Value_Sound_Intensity = 0x0E40,  ///< 14.064 - Sound intensity W/m²
    DPT_Value_Speed = 0x0E41,            ///< 14.065 - Speed m/s
    DPT_Value_Stress = 0x0E42,           ///< 14.066 - Stress Pa
    DPT_Value_Surface_Tension = 0x0E43,  ///< 14.067 - Surface tension N/m
    DPT_Value_Common_Temperature = 0x0E44,     ///< 14.068 - Temperature °C
    DPT_Value_Absolute_Temperature = 0x0E45,   ///< 14.069 - Absolute temperature K
    DPT_Value_TemperatureDifference = 0x0E46,  ///< 14.070 - Temperature difference K
    DPT_Value_Thermal_Capacity = 0x0E47, ///< 14.071 - Thermal capacity J/K
    DPT_Value_Thermal_Conductivity = 0x0E48,   ///< 14.072 - Thermal conductivity W/(m·K)
    DPT_Value_ThermoelectricPower = 0x0E49,    ///< 14.073 - Thermoelectric power V/K
    DPT_Value_Time = 0x0E4A,             ///< 14.074 - Time s
    DPT_Value_Torque = 0x0E4B,           ///< 14.075 - Torque N·m
    DPT_Value_Volume = 0x0E4C,           ///< 14.076 - Volume m³
    DPT_Value_Volume_Flux = 0x0E4D,      ///< 14.077 - Volume flux m³/s
    DPT_Value_Weight = 0x0E4E,           ///< 14.078 - Weight N
    DPT_Value_Work = 0x0E4F,             ///< 14.079 - Work J
    
    // DPT 15 - Access data
    DPT_Access_Data = 0x0F00,       ///< 15.000 - Access data
    
    // DPT 16 - String
    DPT_String_ASCII = 0x1000,      ///< 16.000 - String ASCII (14 chars)
    DPT_String_8859_1 = 0x1001,     ///< 16.001 - String ISO 8859-1
    
    // DPT 17 - Scene number
    DPT_SceneNumber = 0x1101,       ///< 17.001 - Scene number (0-63)
    
    // DPT 18 - Scene control
    DPT_SceneControl = 0x1201,      ///< 18.001 - Scene control
    
    // DPT 19 - Date/Time
    DPT_DateTime = 0x1301,          ///< 19.001 - Date and time
    
    // DPT 20 - 1-byte enum
    DPT_HVAC_Mode = 0x1466,         ///< 20.102 - HVAC mode
    DPT_DHW_Mode = 0x1467,          ///< 20.103 - DHW mode
    DPT_HVACContrMode = 0x1469,     ///< 20.105 - HVAC control mode
    
    // DPT 232 - RGB Color
    DPT_Colour_RGB = 0xE800,        ///< 232.600 - RGB color value
    
    // Unknown/Custom
    DPT_Unknown = 0xFFFF
};

/**
 * @brief Get datapoint type size in bytes
 */
int GetDPTSize(KNXDatapointType dpt);

/**
 * @brief Get datapoint type name
 */
std::string GetDPTName(KNXDatapointType dpt);

// ============================================================================
// KNX TELEGRAM
// ============================================================================

/**
 * @brief KNX Application Protocol Control Information
 */
enum class KNXAPCI : uint8_t {
    GroupValueRead = 0x00,
    GroupValueResponse = 0x01,
    GroupValueWrite = 0x02,
    IndividualAddressWrite = 0x03,
    IndividualAddressRequest = 0x04,
    IndividualAddressResponse = 0x05,
    ADCRead = 0x06,
    ADCResponse = 0x07,
    MemoryRead = 0x08,
    MemoryResponse = 0x09,
    MemoryWrite = 0x0A,
    UserMessage = 0x0B,
    DeviceDescriptorRead = 0x0C,
    DeviceDescriptorResponse = 0x0D,
    Restart = 0x0E,
    Escape = 0x0F
};

/**
 * @brief KNX Priority levels
 */
enum class KNXPriority : uint8_t {
    System = 0x00,
    Normal = 0x01,
    Urgent = 0x02,
    Low = 0x03
};

/**
 * @brief KNX Telegram structure
 */
struct KNXTelegram {
    KNXIndividualAddress Source;
    KNXGroupAddress Destination;
    bool IsGroupAddress = true;
    KNXPriority Priority = KNXPriority::Low;
    bool Repeated = false;
    KNXAPCI APCI = KNXAPCI::GroupValueWrite;
    std::vector<uint8_t> Data;
    
    // Encode to raw bytes
    std::vector<uint8_t> Encode() const;
    
    // Decode from raw bytes
    static KNXTelegram Decode(const std::vector<uint8_t>& raw);
    
    // Get data as specific type
    bool GetBool() const;
    uint8_t GetUInt8() const;
    int8_t GetInt8() const;
    uint16_t GetUInt16() const;
    int16_t GetInt16() const;
    uint32_t GetUInt32() const;
    int32_t GetInt32() const;
    float GetFloat16() const;  // DPT 9.x format
    float GetFloat32() const;  // DPT 14.x format
    std::string GetString() const;
    
    // Set data from specific type
    void SetBool(bool value);
    void SetUInt8(uint8_t value);
    void SetInt8(int8_t value);
    void SetUInt16(uint16_t value);
    void SetInt16(int16_t value);
    void SetUInt32(uint32_t value);
    void SetInt32(int32_t value);
    void SetFloat16(float value);  // DPT 9.x format
    void SetFloat32(float value);  // DPT 14.x format
    void SetString(const std::string& value, size_t maxLen = 14);
    void SetScaling(uint8_t percent);  // 0-100 to 0-255
    uint8_t GetScaling() const;        // 0-255 to 0-100
};

// ============================================================================
// KNXnet/IP TYPES
// ============================================================================

/**
 * @brief KNXnet/IP Service types
 */
enum class KNXnetIPServiceType : uint16_t {
    // Core services
    SearchRequest = 0x0201,
    SearchResponse = 0x0202,
    DescriptionRequest = 0x0203,
    DescriptionResponse = 0x0204,
    ConnectRequest = 0x0205,
    ConnectResponse = 0x0206,
    ConnectionstateRequest = 0x0207,
    ConnectionstateResponse = 0x0208,
    DisconnectRequest = 0x0209,
    DisconnectResponse = 0x020A,
    
    // Device Management
    DeviceConfigurationRequest = 0x0310,
    DeviceConfigurationAck = 0x0311,
    
    // Tunneling
    TunnelingRequest = 0x0420,
    TunnelingAck = 0x0421,
    
    // Routing
    RoutingIndication = 0x0530,
    RoutingLostMessage = 0x0531,
    RoutingBusy = 0x0532,
    
    // Remote Logging
    RemoteDiagRequest = 0x0740,
    RemoteDiagResponse = 0x0741,
    RemoteBasicConfigRequest = 0x0742,
    RemoteResetRequest = 0x0743
};

/**
 * @brief KNXnet/IP Connection types
 */
enum class KNXnetIPConnectionType : uint8_t {
    DeviceManagement = 0x03,
    Tunnel = 0x04,
    RemoteLogging = 0x06,
    RemoteConfig = 0x07,
    ObjectServer = 0x08
};

/**
 * @brief KNXnet/IP Tunnel layer
 */
enum class KNXnetIPTunnelLayer : uint8_t {
    LinkLayer = 0x02,
    RawLayer = 0x04,
    BusMonitorLayer = 0x80
};

/**
 * @brief KNXnet/IP Error codes
 */
enum class KNXnetIPError : uint8_t {
    NoError = 0x00,
    HostProtocolType = 0x01,
    VersionNotSupported = 0x02,
    SequenceNumber = 0x04,
    ConnectionID = 0x21,
    ConnectionType = 0x22,
    ConnectionOption = 0x23,
    NoMoreConnections = 0x24,
    NoMoreUniqueConnections = 0x25,
    DataConnection = 0x26,
    KNXConnection = 0x27,
    TunnelingLayer = 0x29
};

/**
 * @brief KNXnet/IP Device information
 */
struct KNXnetIPDeviceInfo {
    std::string Name;
    std::string IPAddress;
    uint16_t Port = 3671;
    KNXIndividualAddress IndividualAddress;
    std::string SerialNumber;
    std::string MACAddress;
    uint16_t ProjectInstallationID = 0;
    bool SupportsRouting = false;
    bool SupportsTunneling = false;
    
    std::chrono::system_clock::time_point LastSeen;
};

// ============================================================================
// KNX DEVICE AND GROUP OBJECT
// ============================================================================

/**
 * @brief KNX Group Object (communication object)
 */
struct KNXGroupObject {
    KNXGroupAddress Address;
    std::string Name;
    std::string Description;
    KNXDatapointType DPT = KNXDatapointType::DPT_Unknown;
    
    bool ReadEnabled = true;
    bool WriteEnabled = true;
    bool TransmitEnabled = true;
    bool UpdateEnabled = true;
    
    // Current value
    std::vector<uint8_t> Value;
    std::chrono::system_clock::time_point LastUpdate;
    
    // Associated devices (physical addresses that respond to this group)
    std::set<KNXIndividualAddress> AssociatedDevices;
};

/**
 * @brief KNX Device
 */
struct KNXDevice {
    KNXIndividualAddress Address;
    std::string Name;
    std::string Description;
    std::string Manufacturer;
    std::string ProductName;
    std::string SerialNumber;
    std::string ApplicationProgram;
    
    // Communication objects
    std::vector<KNXGroupObject> GroupObjects;
    
    // State
    bool IsReachable = false;
    std::chrono::system_clock::time_point LastSeen;
};

/**
 * @brief KNX Location/Room
 */
struct KNXLocation {
    std::string Name;
    std::string Description;
    std::vector<KNXIndividualAddress> Devices;
    std::vector<KNXGroupAddress> Functions;
};

/**
 * @brief KNX Project (ETS export)
 */
struct KNXProject {
    std::string Name;
    std::string Description;
    std::string ETSVersion;
    std::string ExportDate;
    
    std::map<uint16_t, KNXDevice> Devices;  // Indexed by raw address
    std::map<uint16_t, KNXGroupObject> GroupObjects;  // Indexed by raw address
    std::vector<KNXLocation> Locations;
};

// ============================================================================
// CALLBACKS
// ============================================================================

using KNXTelegramCallback = std::function<void(const KNXTelegram&)>;
using KNXDeviceCallback = std::function<void(const KNXnetIPDeviceInfo&)>;
using KNXGroupValueCallback = std::function<void(const KNXGroupAddress&, const std::vector<uint8_t>&)>;
using KNXConnectionCallback = std::function<void(bool connected, const std::string& message)>;

// ============================================================================
// KNX PROTOCOL CLASS
// ============================================================================

/**
 * @brief KNX Protocol implementation using KNXnet/IP
 * 
 * Provides KNX building automation integration:
 * - KNXnet/IP tunneling and routing
 * - Group address communication
 * - Device discovery
 * - ETS project import
 * - Datapoint type conversion
 */
class KNXProtocol : public SmartHomeProtocolBase {
public:
    KNXProtocol();
    virtual ~KNXProtocol();
    
    // ===== ISmartHomeProtocol Implementation =====
    
    bool Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override { return initialized; }
    
    SmartHomeProtocolType GetType() const override { return SmartHomeProtocolType::KNX; }
    std::string GetName() const override { return "KNX"; }
    std::string GetVersion() const override { return "1.0.0"; }
    
    bool StartDiscovery(int timeoutSeconds) override;
    void StopDiscovery() override;
    bool IsDiscovering() const override { return discovering; }
    std::vector<SmartHomeDeviceInfo> GetDiscoveredDevices() const override;
    
    bool PairDevice(const std::string& deviceId,
                   const std::map<std::string, std::string>& params) override;
    bool UnpairDevice(const std::string& deviceId) override;
    
    bool SendCommand(const std::string& deviceId, const std::string& command,
                    const std::map<std::string, std::string>& params) override;
    
    bool GetDeviceState(const std::string& deviceId,
                       std::map<std::string, std::string>& state) override;
    
    std::vector<SmartHomeDeviceInfo> GetPairedDevices() override;
    
    // ===== KNXnet/IP Connection =====
    
    /**
     * @brief Set gateway IP address
     * @param ip Gateway IP address
     * @param port Port (default 3671)
     */
    void SetGateway(const std::string& ip, uint16_t port = 3671);
    
    /**
     * @brief Get configured gateway
     * @return Gateway IP:port
     */
    std::string GetGateway() const;
    
    /**
     * @brief Connect to KNXnet/IP gateway
     * @return true if connected
     */
    bool Connect();
    
    /**
     * @brief Disconnect from gateway
     */
    void Disconnect();
    
    /**
     * @brief Check if connected
     * @return true if connected
     */
    bool IsConnected() const { return connected; }
    
    /**
     * @brief Enable routing mode (multicast)
     * @param enable Enable/disable
     */
    void EnableRouting(bool enable);
    
    /**
     * @brief Check if routing mode enabled
     * @return true if routing
     */
    bool IsRoutingEnabled() const { return routingEnabled; }
    
    /**
     * @brief Search for KNXnet/IP devices on network
     * @param timeoutMs Timeout in milliseconds
     * @return List of discovered devices
     */
    std::vector<KNXnetIPDeviceInfo> SearchDevices(int timeoutMs = 3000);
    
    // ===== Group Communication =====
    
    /**
     * @brief Write value to group address
     * @param address Group address
     * @param value Raw value bytes
     * @return true if sent
     */
    bool GroupWrite(const KNXGroupAddress& address, const std::vector<uint8_t>& value);
    
    /**
     * @brief Write boolean to group address
     * @param address Group address
     * @param value Boolean value
     * @return true if sent
     */
    bool GroupWriteBool(const KNXGroupAddress& address, bool value);
    
    /**
     * @brief Write scaling value (0-100%) to group address
     * @param address Group address
     * @param percent Percent value (0-100)
     * @return true if sent
     */
    bool GroupWriteScaling(const KNXGroupAddress& address, uint8_t percent);
    
    /**
     * @brief Write 2-byte float to group address (DPT 9.x)
     * @param address Group address
     * @param value Float value
     * @return true if sent
     */
    bool GroupWriteFloat16(const KNXGroupAddress& address, float value);
    
    /**
     * @brief Write 4-byte float to group address (DPT 14.x)
     * @param address Group address
     * @param value Float value
     * @return true if sent
     */
    bool GroupWriteFloat32(const KNXGroupAddress& address, float value);
    
    /**
     * @brief Write unsigned byte to group address
     * @param address Group address
     * @param value Byte value
     * @return true if sent
     */
    bool GroupWriteUInt8(const KNXGroupAddress& address, uint8_t value);
    
    /**
     * @brief Write unsigned 16-bit to group address
     * @param address Group address
     * @param value 16-bit value
     * @return true if sent
     */
    bool GroupWriteUInt16(const KNXGroupAddress& address, uint16_t value);
    
    /**
     * @brief Write unsigned 32-bit to group address
     * @param address Group address
     * @param value 32-bit value
     * @return true if sent
     */
    bool GroupWriteUInt32(const KNXGroupAddress& address, uint32_t value);
    
    /**
     * @brief Write RGB color to group address (DPT 232.600)
     * @param address Group address
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     * @return true if sent
     */
    bool GroupWriteRGB(const KNXGroupAddress& address, uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief Write scene number to group address (DPT 17.001)
     * @param address Group address
     * @param scene Scene number (0-63)
     * @return true if sent
     */
    bool GroupWriteScene(const KNXGroupAddress& address, uint8_t scene);
    
    /**
     * @brief Write string to group address (DPT 16.x)
     * @param address Group address
     * @param text String (max 14 chars)
     * @return true if sent
     */
    bool GroupWriteString(const KNXGroupAddress& address, const std::string& text);
    
    /**
     * @brief Request read from group address
     * @param address Group address
     * @return true if request sent
     */
    bool GroupRead(const KNXGroupAddress& address);
    
    /**
     * @brief Get cached value for group address
     * @param address Group address
     * @return Cached value (empty if not available)
     */
    std::vector<uint8_t> GetGroupValue(const KNXGroupAddress& address) const;
    
    // ===== Device Commands =====
    
    /**
     * @brief Switch on (DPT 1.001)
     * @param address Group address
     * @return true if sent
     */
    bool SwitchOn(const KNXGroupAddress& address);
    
    /**
     * @brief Switch off (DPT 1.001)
     * @param address Group address
     * @return true if sent
     */
    bool SwitchOff(const KNXGroupAddress& address);
    
    /**
     * @brief Toggle switch state
     * @param address Group address
     * @return true if sent
     */
    bool SwitchToggle(const KNXGroupAddress& address);
    
    /**
     * @brief Set dimmer level (DPT 5.001)
     * @param address Group address
     * @param percent Level 0-100%
     * @return true if sent
     */
    bool SetDimLevel(const KNXGroupAddress& address, uint8_t percent);
    
    /**
     * @brief Start dimming (DPT 3.007)
     * @param address Group address
     * @param up true=up, false=down
     * @param steps Dimming steps (1-7, 0=break)
     * @return true if sent
     */
    bool StartDimming(const KNXGroupAddress& address, bool up, uint8_t steps = 7);
    
    /**
     * @brief Stop dimming (DPT 3.007)
     * @param address Group address
     * @return true if sent
     */
    bool StopDimming(const KNXGroupAddress& address);
    
    /**
     * @brief Set blind position (DPT 5.001)
     * @param address Group address
     * @param percent Position 0-100% (0=open, 100=closed)
     * @return true if sent
     */
    bool SetBlindPosition(const KNXGroupAddress& address, uint8_t percent);
    
    /**
     * @brief Set blind slat angle (DPT 5.001)
     * @param address Group address
     * @param percent Angle 0-100%
     * @return true if sent
     */
    bool SetBlindSlat(const KNXGroupAddress& address, uint8_t percent);
    
    /**
     * @brief Move blind up/down (DPT 1.008)
     * @param address Group address
     * @param down true=down, false=up
     * @return true if sent
     */
    bool MoveBlind(const KNXGroupAddress& address, bool down);
    
    /**
     * @brief Stop blind movement (DPT 1.007)
     * @param address Group address
     * @return true if sent
     */
    bool StopBlind(const KNXGroupAddress& address);
    
    /**
     * @brief Set HVAC mode (DPT 20.102)
     * @param address Group address
     * @param mode Mode (0=Auto, 1=Comfort, 2=Standby, 3=Economy, 4=Protection)
     * @return true if sent
     */
    bool SetHVACMode(const KNXGroupAddress& address, uint8_t mode);
    
    /**
     * @brief Set temperature setpoint (DPT 9.001)
     * @param address Group address
     * @param celsius Temperature in Celsius
     * @return true if sent
     */
    bool SetTemperature(const KNXGroupAddress& address, float celsius);
    
    /**
     * @brief Activate scene (DPT 18.001)
     * @param address Group address
     * @param scene Scene number (0-63)
     * @param store true=store scene, false=recall scene
     * @return true if sent
     */
    bool ActivateScene(const KNXGroupAddress& address, uint8_t scene, bool store = false);
    
    // ===== Project Import =====
    
    /**
     * @brief Import ETS project file
     * @param filePath Path to .knxproj or .esf file
     * @return true if imported
     */
    bool ImportProject(const std::string& filePath);
    
    /**
     * @brief Import group addresses from CSV
     * @param filePath Path to CSV file
     * @return true if imported
     */
    bool ImportGroupAddresses(const std::string& filePath);
    
    /**
     * @brief Get imported project
     * @return Project data
     */
    const KNXProject& GetProject() const { return project; }
    
    /**
     * @brief Clear project data
     */
    void ClearProject();
    
    // ===== Group Object Management =====
    
    /**
     * @brief Register a group object
     * @param obj Group object definition
     */
    void RegisterGroupObject(const KNXGroupObject& obj);
    
    /**
     * @brief Unregister a group object
     * @param address Group address
     */
    void UnregisterGroupObject(const KNXGroupAddress& address);
    
    /**
     * @brief Get registered group object
     * @param address Group address
     * @return Group object (nullptr if not found)
     */
    const KNXGroupObject* GetGroupObject(const KNXGroupAddress& address) const;
    
    /**
     * @brief Get all registered group objects
     * @return Map of group objects
     */
    std::map<uint16_t, KNXGroupObject> GetGroupObjects() const;
    
    // ===== Callbacks =====
    
    void SetOnTelegram(KNXTelegramCallback callback) { onTelegram = callback; }
    void SetOnGroupValue(KNXGroupValueCallback callback) { onGroupValue = callback; }
    void SetOnDeviceDiscovered(KNXDeviceCallback callback) { onDeviceDiscovered = callback; }
    void SetOnConnection(KNXConnectionCallback callback) { onConnection = callback; }

private:
    // ===== Network Communication =====
    
    bool SendTelegram(const KNXTelegram& telegram);
    void ReceiveLoop();
    void HeartbeatLoop();
    
    bool SendKNXnetIPFrame(KNXnetIPServiceType service, const std::vector<uint8_t>& data);
    bool ProcessKNXnetIPFrame(const std::vector<uint8_t>& frame);
    
    void HandleSearchResponse(const std::vector<uint8_t>& data);
    void HandleConnectResponse(const std::vector<uint8_t>& data);
    void HandleConnectionstateResponse(const std::vector<uint8_t>& data);
    void HandleDisconnectResponse(const std::vector<uint8_t>& data);
    void HandleTunnelingRequest(const std::vector<uint8_t>& data);
    void HandleTunnelingAck(const std::vector<uint8_t>& data);
    void HandleRoutingIndication(const std::vector<uint8_t>& data);
    
    void SendTunnelingAck(uint8_t channelId, uint8_t sequenceCounter);
    void SendConnectionstateRequest();
    
    // ===== Data Conversion =====
    
    static float DecodeFloat16(uint8_t high, uint8_t low);
    static void EncodeFloat16(float value, uint8_t& high, uint8_t& low);
    static float DecodeFloat32(const uint8_t* data);
    static void EncodeFloat32(float value, uint8_t* data);
    
    // ===== Helper Methods =====
    
    SmartHomeDeviceInfo ConvertToDeviceInfo(const KNXGroupObject& obj) const;
    SmartHomeDeviceCategory DetermineCategory(const KNXGroupObject& obj) const;
    std::string MakeDeviceId(const KNXGroupAddress& address) const;
    KNXGroupAddress ParseDeviceId(const std::string& deviceId) const;
    
    void NotifyTelegram(const KNXTelegram& telegram);
    void NotifyGroupValue(const KNXGroupAddress& address, const std::vector<uint8_t>& value);
    
    // ===== State =====
    
    std::atomic<bool> initialized{false};
    std::atomic<bool> discovering{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> routingEnabled{false};
    
    std::string gatewayIP = "192.168.1.1";
    uint16_t gatewayPort = 3671;
    
    // KNXnet/IP connection
    int udpSocket = -1;
    uint8_t channelId = 0;
    uint8_t sequenceCounter = 0;
    uint8_t receivedSequence = 0;
    
    // Threads
    std::thread receiveThread;
    std::thread heartbeatThread;
    std::atomic<bool> stopThreads{false};
    
    // Project and group objects
    KNXProject project;
    mutable std::mutex groupObjectsMutex;
    std::map<uint16_t, KNXGroupObject> groupObjects;
    
    // Value cache
    mutable std::mutex valueCacheMutex;
    std::map<uint16_t, std::vector<uint8_t>> valueCache;
    std::map<uint16_t, std::chrono::system_clock::time_point> valueCacheTime;
    
    // Discovered devices
    mutable std::mutex discoveredMutex;
    std::vector<KNXnetIPDeviceInfo> discoveredGateways;
    std::vector<SmartHomeDeviceInfo> discoveredDevices;
    
    // Callbacks
    KNXTelegramCallback onTelegram;
    KNXGroupValueCallback onGroupValue;
    KNXDeviceCallback onDeviceDiscovered;
    KNXConnectionCallback onConnection;
    
    // Pending acknowledgments
    std::mutex ackMutex;
    std::condition_variable ackCondition;
    bool ackReceived = false;
    
    // Multicast for routing
    static constexpr const char* KNX_MULTICAST_ADDR = "224.0.23.12";
    static constexpr uint16_t KNX_PORT = 3671;
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

/**
 * @brief Create KNX protocol instance
 * @return Protocol instance
 */
std::shared_ptr<ISmartHomeProtocol> CreateKNXProtocol();

} // namespace SmartHome
} // namespace UltraCanvas
