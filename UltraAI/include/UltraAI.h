// UltraAI/include/UltraAI.h
// Umbrella header for the UltraAI module — a sibling of UltraCanvas.
// Pulls in every capability interface so applications can `#include <UltraAI.h>`.
// Version: 0.1.2 - includes UltraAIBatch.h
// Version: 0.1.1 - includes UltraAIAgent.h
// Version: 0.1.0
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAIRouting.h"
#include "UltraAITextLLM.h"
#include "UltraAIEmbeddings.h"
#include "UltraAISpeechToText.h"
#include "UltraAITextToSpeech.h"
#include "UltraAIImageGen.h"
#include "UltraAIVisionAnalyzer.h"
#include "UltraAITranslator.h"
#include "UltraAITextLLMTranslator.h"
#include "UltraAIVideoGen.h"
#include "UltraAIMusicGen.h"
#include "UltraAICodeAssist.h"
#include "UltraAIAgent.h"
#include "UltraAIBatch.h"

#define ULTRA_AI_VERSION_MAJOR 0
#define ULTRA_AI_VERSION_MINOR 1
#define ULTRA_AI_VERSION_PATCH 0
