// UltraAI/include/UltraAI.h
// Umbrella header for the UltraAI module — a sibling of UltraCanvas.
// Pulls in every capability interface so applications can `#include <UltraAI.h>`.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAITextLLM.h"
#include "UltraAIEmbeddings.h"
#include "UltraAISpeechToText.h"
#include "UltraAITextToSpeech.h"
#include "UltraAIImageGen.h"
#include "UltraAIVisionAnalyzer.h"

#define ULTRA_AI_VERSION_MAJOR 0
#define ULTRA_AI_VERSION_MINOR 1
#define ULTRA_AI_VERSION_PATCH 0
