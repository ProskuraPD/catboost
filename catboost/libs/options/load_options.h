#pragma once

#include "enums.h"
#include "cross_validation_params.h"

#include <catboost/libs/data_util/line_data_reader.h>
#include <catboost/libs/data_util/path_with_scheme.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

namespace NCatboostOptions {
    struct TDsvPoolFormatParams {
        NCB::TDsvFormatOptions Format;
        NCB::TPathWithScheme CdFilePath;

        TDsvPoolFormatParams() = default;

        void Validate() const;
    };

    struct TPoolLoadParams {
        TCvDataPartitionParams CvParams;

        TDsvPoolFormatParams DsvPoolFormatParams;

        NCB::TPathWithScheme LearnSetPath;
        TVector<NCB::TPathWithScheme> TestSetPaths;

        NCB::TPathWithScheme PairsFilePath;
        NCB::TPathWithScheme TestPairsFilePath;

        NCB::TPathWithScheme GroupWeightsFilePath;
        NCB::TPathWithScheme TestGroupWeightsFilePath;

        TVector<ui32> IgnoredFeatures;
        TString BordersFile;

        TPoolLoadParams() = default;

        void Validate() const;
        void Validate(TMaybe<ETaskType> taskType) const;
    };
}
