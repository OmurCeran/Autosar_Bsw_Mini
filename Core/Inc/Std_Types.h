/**
 * @file    Std_Types.h
 * @brief   AUTOSAR Standard Type Definitions (Simplified)
 *
 * Maps to real AUTOSAR: Std_Types.h from BSW General
 * In real AUTOSAR, this is auto-generated. Here we define manually.
 */

#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* --- AUTOSAR Standard Return Type --- */
typedef uint8_t Std_ReturnType;

#define E_OK        ((Std_ReturnType)0x00U)
#define E_NOT_OK    ((Std_ReturnType)0x01U)

/* --- AUTOSAR Standard Types --- */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;
typedef float    float32;
typedef double   float64;

/* --- Boolean --- */
#ifndef TRUE
#define TRUE    ((boolean)1U)
#endif
#ifndef FALSE
#define FALSE   ((boolean)0U)
#endif
typedef uint8 boolean;

/* --- NULL Pointer --- */
#ifndef NULL_PTR
#define NULL_PTR    ((void *)0)
#endif

/* --- Std_VersionInfoType (simplified) --- */
typedef struct {
    uint16 vendorID;
    uint16 moduleID;
    uint8  sw_major_version;
    uint8  sw_minor_version;
    uint8  sw_patch_version;
} Std_VersionInfoType;

#endif /* STD_TYPES_H */