#include "helpers.h"

const TMirrorBuffer<ui32>& NCatboostCuda::GetBinsForModel(TScopedCacheHolder& cacheHolder,
                                                          const NCatboostCuda::TBinarizedFeaturesManager& featuresManager,
                                                          const NCatboostCuda::TFeatureParallelDataSet& dataSet,
                                                          const NCatboostCuda::TObliviousTreeStructure& structure) {
    bool hasPermutationCtrs = HasPermutationDependentSplit(structure, featuresManager);
    const auto& scope = hasPermutationCtrs ? dataSet.GetPermutationDependentScope() : dataSet.GetPermutationIndependentScope();
    return cacheHolder.Cache(scope, structure, [&]() -> TMirrorBuffer<ui32> {
        const bool hasHistory = dataSet.HasCtrHistoryDataSet();
        TMirrorBuffer<ui32> learnBins;
        TMirrorBuffer<ui32> testBins;

        if (hasHistory) {
            learnBins = TMirrorBuffer<ui32>::Create(dataSet.LinkedHistoryForCtr().GetSamplesMapping());
            testBins = TMirrorBuffer<ui32>::Create(dataSet.GetSamplesMapping());
        } else {
            learnBins = TMirrorBuffer<ui32>::Create(dataSet.GetSamplesMapping());
        }

        {
            TTreeUpdater builder(cacheHolder,
                                 featuresManager,
                                 dataSet.GetCtrTargets(),
                                 hasHistory ? dataSet.LinkedHistoryForCtr() : dataSet,
                                 learnBins,
                                 hasHistory ? &dataSet : nullptr,
                                 hasHistory ? &testBins : nullptr);

            for (auto& split : structure.Splits) {
                builder.AddSplit(split);
            }
        }

        if (hasHistory) {
            cacheHolder.CacheOnly(dataSet.LinkedHistoryForCtr(), structure, [&]() -> TMirrorBuffer<ui32> {
                return std::move(learnBins);
            });
        }
        return hasHistory ? std::move(testBins) : std::move(learnBins);
    });
}

void NCatboostCuda::CacheBinsForModel(TScopedCacheHolder& cacheHolder,
                                      const NCatboostCuda::TBinarizedFeaturesManager& featuresManager,
                                      const NCatboostCuda::TFeatureParallelDataSet& dataSet,
                                      const NCatboostCuda::TObliviousTreeStructure& structure,
                                      TMirrorBuffer<ui32>&& bins) {
    bool hasPermutationCtrs = HasPermutationDependentSplit(structure, featuresManager);
    const auto& scope = hasPermutationCtrs ? dataSet.GetPermutationDependentScope() : dataSet.GetPermutationIndependentScope();
    cacheHolder.CacheOnly(scope, structure, [&]() -> TMirrorBuffer<ui32> {
        TMirrorBuffer<ui32> cachedBins = std::move(bins);
        return cachedBins;
    });
}

bool NCatboostCuda::HasPermutationDependentSplit(const NCatboostCuda::TObliviousTreeStructure& structure,
                                                 const NCatboostCuda::TBinarizedFeaturesManager& featuresManager) {
    for (const auto& split : structure.Splits) {
        if (featuresManager.IsCtr(split.FeatureId)) {
            auto ctr = featuresManager.GetCtr(split.FeatureId);
            if (featuresManager.IsPermutationDependent(ctr)) {
                return true;
            }
        }
    }
    return false;
}

void NCatboostCuda::PrintBestScore(const NCatboostCuda::TBinarizedFeaturesManager& featuresManager,
                                   const NCatboostCuda::TBinarySplit& bestSplit, double score, ui32 depth) {
    TString splitTypeMessage;

    if (bestSplit.SplitType == EBinSplitType::TakeBin) {
        splitTypeMessage = "TakeBin";
    } else {
        const auto& borders =  featuresManager.GetBorders(bestSplit.FeatureId);
        auto nanMode = featuresManager.GetNanMode(bestSplit.FeatureId);
        TStringBuilder messageBuilder;
        if (nanMode == ENanMode::Forbidden) {
            messageBuilder << ">" << featuresManager.GetBorders(bestSplit.FeatureId)[bestSplit.BinIdx];
        } else if (nanMode == ENanMode::Min) {
            if (bestSplit.BinIdx > 0) {
                messageBuilder << ">" << featuresManager.GetBorders(bestSplit.FeatureId)[bestSplit.BinIdx - 1];
            } else {
                messageBuilder << "== -inf (nan)";
            }
        } else {
            Y_VERIFY(nanMode == ENanMode::Max);
            if (bestSplit.BinIdx < borders.size()) {
                messageBuilder << ">" << featuresManager.GetBorders(bestSplit.FeatureId)[bestSplit.BinIdx];
            } else {
                Y_VERIFY(bestSplit.BinIdx == borders.size());
                messageBuilder << "== +inf (nan)";
            }
        }

        splitTypeMessage = messageBuilder;
    }
    TStringBuilder logEntry;
    logEntry
        << "Best split for depth " << depth << ": " << bestSplit.FeatureId << " / " << bestSplit.BinIdx << " ("
        << splitTypeMessage << ")"
        << " with score " << score;
    if (featuresManager.IsCtr(bestSplit.FeatureId)) {
        logEntry
            << " tensor : " << featuresManager.GetCtr(bestSplit.FeatureId).FeatureTensor << "  (ctr type "
            << featuresManager.GetCtr(bestSplit.FeatureId).Configuration.Type << ")";
    }
    CATBOOST_INFO_LOG << logEntry << Endl;
}

NCatboostCuda::TBinarySplit NCatboostCuda::ToSplit(const NCatboostCuda::TBinarizedFeaturesManager& manager, const TBestSplitProperties& props) {
    Y_VERIFY(props.Defined());
    TBinarySplit bestSplit;
    bestSplit.FeatureId = props.FeatureId;
    bestSplit.BinIdx = props.BinId;
    //We need to adjust binIdx. Float arithmetic could generate empty bin splits for ctrs
    if (manager.IsCat(props.FeatureId)) {
        bestSplit.SplitType = EBinSplitType::TakeBin;
        bestSplit.BinIdx = Min<ui32>(manager.GetBinCount(bestSplit.FeatureId), bestSplit.BinIdx);
    } else {
        bestSplit.SplitType = EBinSplitType::TakeGreater;
        bestSplit.BinIdx = Min<ui32>(manager.GetBorders(bestSplit.FeatureId).size() - 1, bestSplit.BinIdx);
    }
    return bestSplit;
}
