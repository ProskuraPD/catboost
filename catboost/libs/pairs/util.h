#pragma once

#include <catboost/libs/data_new/objects_grouping.h>
#include <catboost/libs/data_types/pair.h>

#include <catboost/libs/helpers/restorable_rng.h>

#include <util/generic/array_ref.h>
#include <util/generic/vector.h>


void GeneratePairLogitPairs(
    const NCB::TObjectsGrouping& objectsGrouping,
    TConstArrayRef<float> targetId,
    int maxPairCount,
    TRestorableFastRng64* rand,
    TVector<TPair>* result);
