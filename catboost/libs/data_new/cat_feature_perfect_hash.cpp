#include "cat_feature_perfect_hash.h"

#include <util/stream/output.h>


template <>
void Out<NCB::TCatFeatureUniqueValuesCounts>(IOutputStream& out, NCB::TCatFeatureUniqueValuesCounts counts) {
    out << counts.OnLearnOnly << ',' << counts.OnAll;
}


namespace NCB {

    bool TCatFeaturesPerfectHash::operator==(const TCatFeaturesPerfectHash& rhs) const {
        if (CatFeatureUniqValuesCountsVector != rhs.CatFeatureUniqValuesCountsVector) {
            return false;
        }

        if (!HasHashInRam) {
            Load();
        }
        if (!rhs.HasHashInRam) {
            rhs.Load();
        }
        return FeaturesPerfectHash == rhs.FeaturesPerfectHash;
    }


    void TCatFeaturesPerfectHash::UpdateFeaturePerfectHash(
        const TCatFeatureIdx catFeatureIdx,
        TMap<ui32, ui32>&& perfectHash
    ) {
        CheckHasFeature(catFeatureIdx);

        auto& counts = CatFeatureUniqValuesCountsVector[*catFeatureIdx];

        if (counts.OnAll) {
            // already have some data
            // we must update with data that has not less elements than current
            Y_VERIFY((size_t)counts.OnAll <= perfectHash.size());
        } else {
            // first initialization
            counts.OnLearnOnly = (ui32)perfectHash.size();
        }

        // cast is safe because map from ui32 keys can't have more than Max<ui32>() keys
        counts.OnAll = (ui32)perfectHash.size();

        if (!HasHashInRam) {
            Load();
        }
        FeaturesPerfectHash[*catFeatureIdx] = std::move(perfectHash);
    }

    int TCatFeaturesPerfectHash::operator&(IBinSaver& binSaver) {
        if (!binSaver.IsReading()) {
            if (!HasHashInRam) {
                Load();
            }
        }
        binSaver.AddMulti(CatFeatureUniqValuesCountsVector, FeaturesPerfectHash, AllowWriteFiles);
        if (binSaver.IsReading()) {
            HasHashInRam = true;
        }
        return 0;
    }

    ui32 TCatFeaturesPerfectHash::CalcCheckSum() const {
        if (!HasHashInRam) {
            Load();
        }
        ui32 checkSum = 0;
        checkSum = UpdateCheckSum(checkSum, CatFeatureUniqValuesCountsVector);
        checkSum = UpdateCheckSum(checkSum, FeaturesPerfectHash);
        return checkSum;
    }
}
