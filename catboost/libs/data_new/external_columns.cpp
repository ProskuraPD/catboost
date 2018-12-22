#include "external_columns.h"

#include <catboost/libs/helpers/exception.h>
#include <catboost/libs/quantization/utils.h>


namespace NCB {

    THolder<IQuantizedFloatValuesHolder> TExternalFloatValuesHolder::CloneWithNewSubsetIndexing(
        const TFeaturesArraySubsetIndexing* subsetIndexing
    ) const {
        return MakeHolder<TExternalFloatValuesHolder>(
            GetId(),
            SrcData,
            subsetIndexing,
            QuantizedFeaturesInfo
        );
    }

    NCB::TMaybeOwningArrayHolder<ui8> TExternalFloatValuesHolder::ExtractValues(
        NPar::TLocalExecutor* localExecutor
    ) const {
        TVector<ui8> result;
        result.yresize(GetSize());
        TArrayRef<ui8> resultAsArrayRef = result;

        const auto floatFeatureIdx = QuantizedFeaturesInfo->GetPerTypeFeatureIdx<EFeatureType::Float>(*this);
        const auto nanMode = QuantizedFeaturesInfo->GetNanMode(floatFeatureIdx);

        // it's ok even if it is learn data, for learn nans are checked at CalcBordersAndNanMode stage
        bool allowNans = (nanMode != ENanMode::Forbidden) ||
            QuantizedFeaturesInfo->GetFloatFeaturesAllowNansInTestOnly();

        Quantize(
            TMaybeOwningConstArraySubset<float, ui32>(&SrcData, SubsetIndexing),
            allowNans,
            nanMode,
            GetId(),
            QuantizedFeaturesInfo->GetBorders(floatFeatureIdx),
            localExecutor,
            &resultAsArrayRef
        );

        return NCB::TMaybeOwningArrayHolder<ui8>::CreateOwning(std::move(result));
    }


    THolder<IQuantizedCatValuesHolder> TExternalCatValuesHolder::CloneWithNewSubsetIndexing(
        const TFeaturesArraySubsetIndexing* subsetIndexing
    ) const {
        return MakeHolder<TExternalCatValuesHolder>(
            GetId(),
            SrcData,
            subsetIndexing,
            QuantizedFeaturesInfo
        );
    }

    NCB::TMaybeOwningArrayHolder<ui32> TExternalCatValuesHolder::ExtractValues(
        NPar::TLocalExecutor* localExecutor
    ) const {
        TVector<ui32> result;
        result.yresize(GetSize());

        const auto catFeatureIdx = QuantizedFeaturesInfo->GetPerTypeFeatureIdx<EFeatureType::Categorical>(
            *this
        );
        const auto& perfectHash = QuantizedFeaturesInfo->GetCategoricalFeaturesPerfectHash(catFeatureIdx);

        TMaybeOwningConstArraySubset<ui32, ui32>(&SrcData, SubsetIndexing).ParallelForEach(
            [&] (ui32 idx, ui32 srcValue) {
                auto it = perfectHash.find(srcValue); // find is guaranteed to be thread-safe

                // TODO(akhropov): replace by assert for performance?
                CB_ENSURE(it != perfectHash.end(),
                          "Error: hash for feature #" << GetId() << " was not found "
                          << srcValue);

                result[idx] = it->second;
            },
            localExecutor,
            BINARIZATION_BLOCK_SIZE
        );

        return NCB::TMaybeOwningArrayHolder<ui32>::CreateOwning(std::move(result));
    }

}
