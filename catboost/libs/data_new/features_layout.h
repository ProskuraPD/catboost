#pragma once

#include "feature_index.h"

#include <catboost/libs/options/enums.h>
#include <catboost/libs/model/features.h>

#include <library/binsaver/bin_saver.h>
#include <library/dbg_output/dump.h>

#include <util/generic/array_ref.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/generic/string.h>
#include <util/generic/xrange.h>
#include <util/system/yassert.h>


namespace NCB {

    struct TFeatureMetaInfo {
        EFeatureType Type;
        TString Name;
        bool IsIgnored;

        /* some datasets can contain only part of all features present in the whole dataset
         * (e.g. workers in distributed processing)
         * ignored features are always unavailable
         */
        bool IsAvailable;

    public:
        // needed for BinSaver
        TFeatureMetaInfo() = default;

        TFeatureMetaInfo(
            EFeatureType type,
            const TString& name,
            bool isIgnored = false,
            bool isAvailable = true // isIgnored = true overrides this parameter
        )
            : Type(type)
            , Name(name)
            , IsIgnored(isIgnored)
            , IsAvailable(!isIgnored && isAvailable)
        {}

        bool operator==(const TFeatureMetaInfo& rhs) const;

        SAVELOAD(Type, Name, IsIgnored, IsAvailable);
    };

}

template <>
struct TDumper<NCB::TFeatureMetaInfo> {
    template <class S>
    static inline void Dump(S& s, const NCB::TFeatureMetaInfo& featureMetaInfo) {
        s << "Type=" << featureMetaInfo.Type << "\tName=" << featureMetaInfo.Name
          << "\tIsIgnored=" << featureMetaInfo.IsIgnored << "\tIsAvailable=" << featureMetaInfo.IsAvailable;
    }
};



namespace NCB {

    class TFeaturesLayout final : public TAtomicRefCount<TFeaturesLayout> {
    public:
        // needed because of default init in Cython and because of BinSaver
        TFeaturesLayout() = default;

        explicit TFeaturesLayout(const ui32 featureCount);

        TFeaturesLayout(const ui32 featureCount, TVector<ui32> catFeatureIndices, const TVector<TString>& featureId);
        TFeaturesLayout(const TVector<TFloatFeature>& floatFeatures, const TVector<TCatFeature>& catFeatures);

        bool operator==(const TFeaturesLayout& rhs) const;

        SAVELOAD(
            ExternalIdxToMetaInfo,
            FeatureExternalIdxToInternalIdx,
            CatFeatureInternalIdxToExternalIdx,
            FloatFeatureInternalIdxToExternalIdx
        )

        const TFeatureMetaInfo& GetInternalFeatureMetaInfo(
            ui32 internalFeatureIdx,
            EFeatureType type
        ) const {
            return ExternalIdxToMetaInfo[GetExternalFeatureIdx(internalFeatureIdx, type)];
        }

        // prefer this method to GetExternalFeatureIds
        TConstArrayRef<TFeatureMetaInfo> GetExternalFeaturesMetaInfo() const {
            return ExternalIdxToMetaInfo;
        }

        TString GetExternalFeatureDescription(ui32 internalFeatureIdx, EFeatureType type) const {
            return ExternalIdxToMetaInfo[GetExternalFeatureIdx(internalFeatureIdx, type)].Name;
        }
        TVector<TString> GetExternalFeatureIds() const;

        // needed for python-package
        void SetExternalFeatureIds(TConstArrayRef<TString> featureIds);

        ui32 GetExternalFeatureIdx(ui32 internalFeatureIdx, EFeatureType type) const {
            if (type == EFeatureType::Float) {
                return FloatFeatureInternalIdxToExternalIdx[internalFeatureIdx];
            } else {
                return CatFeatureInternalIdxToExternalIdx[internalFeatureIdx];
            }
        }
        ui32 GetInternalFeatureIdx(ui32 externalFeatureIdx) const {
            Y_ASSERT(IsCorrectExternalFeatureIdx(externalFeatureIdx));
            return FeatureExternalIdxToInternalIdx[externalFeatureIdx];
        }
        template <EFeatureType FeatureType>
        TFeatureIdx<FeatureType> GetInternalFeatureIdx(ui32 externalFeatureIdx) const {
            Y_ASSERT(IsCorrectExternalFeatureIdxAndType(externalFeatureIdx, FeatureType));
            return TFeatureIdx<FeatureType>(FeatureExternalIdxToInternalIdx[externalFeatureIdx]);
        }
        EFeatureType GetExternalFeatureType(ui32 externalFeatureIdx) const {
            Y_ASSERT(IsCorrectExternalFeatureIdx(externalFeatureIdx));
            return ExternalIdxToMetaInfo[externalFeatureIdx].Type;
        }
        bool IsCorrectExternalFeatureIdx(ui32 externalFeatureIdx) const {
            return (size_t)externalFeatureIdx < ExternalIdxToMetaInfo.size();
        }

        bool IsCorrectInternalFeatureIdx(ui32 internalFeatureIdx, EFeatureType type) const {
            if (type == EFeatureType::Float) {
                return (size_t)internalFeatureIdx < FloatFeatureInternalIdxToExternalIdx.size();
            } else {
                return (size_t)internalFeatureIdx < CatFeatureInternalIdxToExternalIdx.size();
            }
        }
        bool IsCorrectExternalFeatureIdxAndType(ui32 externalFeatureIdx, EFeatureType type) const {
            if ((size_t)externalFeatureIdx >= ExternalIdxToMetaInfo.size()) {
                return false;
            }
            return ExternalIdxToMetaInfo[externalFeatureIdx].Type == type;
        }

        ui32 GetFloatFeatureCount() const {
            // cast is safe because of size invariant established in constructors
            return (ui32)FloatFeatureInternalIdxToExternalIdx.size();
        }
        ui32 GetCatFeatureCount() const {
            // cast is safe because of size invariant established in constructors
            return (ui32)CatFeatureInternalIdxToExternalIdx.size();
        }
        ui32 GetExternalFeatureCount() const {
            // cast is safe because of size invariant established in constructors
            return (ui32)ExternalIdxToMetaInfo.size();
        }

        ui32 GetFeatureCount(EFeatureType type) const {
            if (type == EFeatureType::Float) {
                return GetFloatFeatureCount();
            } else {
                return GetCatFeatureCount();
            }
        }

        void IgnoreExternalFeature(ui32 externalFeatureIdx) {
            auto& metaInfo = ExternalIdxToMetaInfo[externalFeatureIdx];
            metaInfo.IsIgnored = true;
            metaInfo.IsAvailable = false;
        }

        // indices in list can be outside of range of features in layout - such features are ignored
        void IgnoreExternalFeatures(TConstArrayRef<ui32> ignoredFeatures);

        // Function must get one param -  TFeatureIdx<FeatureType>
        template <EFeatureType FeatureType, class Function>
        void IterateOverAvailableFeatures(Function&& f) const {
            const ui32 perTypeFeatureCount = GetFeatureCount(FeatureType);

            for (auto perTypeFeatureIdx : xrange(perTypeFeatureCount)) {
                if (GetInternalFeatureMetaInfo(perTypeFeatureIdx, FeatureType).IsAvailable) {
                    f(TFeatureIdx<FeatureType>(perTypeFeatureIdx));
                }
            }
        }

        TConstArrayRef<ui32> GetCatFeatureInternalIdxToExternalIdx() const {
            return CatFeatureInternalIdxToExternalIdx;
        }

        bool HasAvailableAndNotIgnoredFeatures() const;

    private:
        TVector<TFeatureMetaInfo> ExternalIdxToMetaInfo;
        TVector<ui32> FeatureExternalIdxToInternalIdx;
        TVector<ui32> CatFeatureInternalIdxToExternalIdx;
        TVector<ui32> FloatFeatureInternalIdxToExternalIdx;
    };

    using TFeaturesLayoutPtr = TIntrusivePtr<TFeaturesLayout>;

    void CheckCompatibleForApply(
        const TFeaturesLayout& learnFeaturesLayout,
        const TFeaturesLayout& applyFeaturesLayout,
        const TString& applyDataName
    );
}


template <>
struct TDumper<NCB::TFeaturesLayout> {
    template <class S>
    static inline void Dump(S& s, const NCB::TFeaturesLayout& featuresLayout) {
        auto externalFeaturesMetaInfo = featuresLayout.GetExternalFeaturesMetaInfo();
        for (auto externalFeatureIdx : xrange(externalFeaturesMetaInfo.size())) {
            s << "externalFeatureIdx=" << externalFeatureIdx
              << "\tinternalFeatureIdx=" << featuresLayout.GetInternalFeatureIdx(externalFeatureIdx)
              << "\tMetaInfo={" << DbgDump(externalFeaturesMetaInfo[externalFeatureIdx]) << "}\n";
        }
    }
};


