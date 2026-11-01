/* Public domain. No rights reserved. Wholly AI-authored. */
/* The OS/2 FONTMETRICS record is 228 bytes, independent of host pointer width. */
#ifndef TWOINE_PMGPI_FONTS_H
#define TWOINE_PMGPI_FONTS_H
#include "os2types.h"
#define FACESIZE 32
#pragma pack(push, 2)
typedef struct _FATTRS
{
    USHORT usRecordLength, fsSelection;
    LONG lMatch;
    CHAR szFacename[FACESIZE];
    USHORT idRegistry, usCodePage;
    LONG lMaxBaselineExt, lAveCharWidth;
    USHORT fsType, fsFontUse;
} FATTRS;
typedef FATTRS *PFATTRS;
typedef struct _PANOSE
{
    BYTE bFamilyType, bSerifStyle, bWeight, bProportion, bContrast, bStrokeVariation;
    BYTE bArmStyle, bLetterform, bMidline, bXHeight, fbPassedISO, fbFailedISO;
} PANOSE;
typedef struct _FONTMETRICS
{
    CHAR szFamilyname[FACESIZE];
    CHAR szFacename[FACESIZE];
    USHORT idRegistry;
    USHORT usCodePage;
    LONG lEmHeight;
    LONG lXHeight;
    LONG lMaxAscender;
    LONG lMaxDescender;
    LONG lLowerCaseAscent;
    LONG lLowerCaseDescent;
    LONG lInternalLeading;
    LONG lExternalLeading;
    LONG lAveCharWidth;
    LONG lMaxCharInc;
    LONG lEmInc;
    LONG lMaxBaselineExt;
    SHORT sCharSlope;
    SHORT sInlineDir;
    SHORT sCharRot;
    USHORT usWeightClass;
    USHORT usWidthClass;
    SHORT sXDeviceRes;
    SHORT sYDeviceRes;
    SHORT sFirstChar;
    SHORT sLastChar;
    SHORT sDefaultChar;
    SHORT sBreakChar;
    SHORT sNominalPointSize;
    SHORT sMinimumPointSize;
    SHORT sMaximumPointSize;
    USHORT fsType;
    USHORT fsDefn;
    USHORT fsSelection;
    USHORT fsCapabilities;
    LONG lSubscriptXSize;
    LONG lSubscriptYSize;
    LONG lSubscriptXOffset;
    LONG lSubscriptYOffset;
    LONG lSuperscriptXSize;
    LONG lSuperscriptYSize;
    LONG lSuperscriptXOffset;
    LONG lSuperscriptYOffset;
    LONG lUnderscoreSize;
    LONG lUnderscorePosition;
    LONG lStrikeoutSize;
    LONG lStrikeoutPosition;
    SHORT sKerningPairs;
    SHORT sFamilyClass;
    LONG lMatch;
    LONG FamilyNameAtom;
    LONG FaceNameAtom;
    PANOSE panose;
} FONTMETRICS;
typedef FONTMETRICS *PFONTMETRICS;
#pragma pack(pop)
#endif
