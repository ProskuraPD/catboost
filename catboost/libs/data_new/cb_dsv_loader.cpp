#include "cb_dsv_loader.h"

#include <catboost/libs/column_description/cd_parser.h>
#include <catboost/libs/data_util/exists_checker.h>
#include <catboost/libs/helpers/mem_usage.h>

#include <library/object_factory/object_factory.h>

#include <util/generic/maybe.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/string/iterator.h>
#include <util/string/split.h>
#include <util/system/types.h>


namespace NCB {

    TCBDsvDataLoader::TCBDsvDataLoader(TDatasetLoaderPullArgs&& args)
        : TCBDsvDataLoader(
            TLineDataLoaderPushArgs {
                GetLineDataReader(args.PoolPath, args.CommonArgs.PoolFormat),
                std::move(args.CommonArgs)
            }
        )
    {
    }

    TCBDsvDataLoader::TCBDsvDataLoader(TLineDataLoaderPushArgs&& args)
        : TAsyncProcDataLoaderBase<TString>(std::move(args.CommonArgs))
        , FieldDelimiter(Args.PoolFormat.Delimiter)
        , LineDataReader(std::move(args.Reader))
    {
        CB_ENSURE(!Args.PairsFilePath.Inited() || CheckExists(Args.PairsFilePath),
                  "TCBDsvDataLoader:PairsFilePath does not exist");
        CB_ENSURE(!Args.GroupWeightsFilePath.Inited() || CheckExists(Args.GroupWeightsFilePath),
                  "TCBDsvDataLoader:GroupWeightsFilePath does not exist");

        TMaybe<TString> header = LineDataReader->GetHeader();
        TMaybe<TVector<TString>> headerColumns;
        if (header) {
            headerColumns = TVector<TString>(StringSplitter(*header).Split(FieldDelimiter));
        }

        TString firstLine;
        CB_ENSURE(LineDataReader->ReadLine(&firstLine), "TCBDsvDataLoader: no data rows in pool");
        const ui32 columnsCount = StringSplitter(firstLine).Split(FieldDelimiter).Count();

        auto columnsDescription = TDataColumnsMetaInfo{ CreateColumnsDescription(columnsCount) };
        auto featureIds = columnsDescription.GenerateFeatureIds(headerColumns);

        DataMetaInfo = TDataMetaInfo(
            std::move(columnsDescription),
            Args.GroupWeightsFilePath.Inited(),
            Args.PairsFilePath.Inited(),
            &featureIds
        );

        AsyncRowProcessor.AddFirstLine(std::move(firstLine));

        ProcessIgnoredFeaturesList(Args.IgnoredFeatures, &DataMetaInfo, &FeatureIgnored);

        AsyncRowProcessor.ReadBlockAsync(GetReadFunc());
    }

    TVector<TColumn> TCBDsvDataLoader::CreateColumnsDescription(ui32 columnsCount) {
        return Args.CdProvider->GetColumnsDescription(columnsCount);
    }


    void TCBDsvDataLoader::StartBuilder(bool inBlock,
                                          ui32 objectCount, ui32 /*offset*/,
                                          IRawObjectsOrderDataVisitor* visitor)
    {
        visitor->Start(inBlock, DataMetaInfo, objectCount, Args.ObjectsOrder, {});
    }


    void TCBDsvDataLoader::ProcessBlock(IRawObjectsOrderDataVisitor* visitor) {
        visitor->StartNextBlock(AsyncRowProcessor.GetParseBufferSize());

        auto& columnsDescription = DataMetaInfo.ColumnsInfo->Columns;

        auto parseBlock = [&](TString& line, int lineIdx) {
            const auto& featuresLayout = *DataMetaInfo.FeaturesLayout;

            ui32 featureId = 0;
            ui32 baselineIdx = 0;

            TVector<float> floatFeatures;
            floatFeatures.yresize(featuresLayout.GetFloatFeatureCount());

            TVector<ui32> catFeatures;
            catFeatures.yresize(featuresLayout.GetCatFeatureCount());

            size_t tokenCount = 0;
            TVector<TStringBuf> tokens = StringSplitter(line).Split(FieldDelimiter);
            try {
                for (const auto& token : tokens) {
                    try {
                        switch (columnsDescription[tokenCount].Type) {
                            case EColumn::Categ: {
                                if (!FeatureIgnored[featureId]) {
                                    const ui32 catFeatureIdx = featuresLayout.GetInternalFeatureIdx(featureId);
                                    if (IsNanValue(token)) {
                                        catFeatures[catFeatureIdx] = visitor->GetCatFeatureValue(featureId, "nan");
                                    } else {
                                        catFeatures[catFeatureIdx] = visitor->GetCatFeatureValue(featureId, token);
                                    }
                                }
                                ++featureId;
                                break;
                            }
                            case EColumn::Num: {
                                if (!FeatureIgnored[featureId]) {
                                    if (!TryParseFloatFeatureValue(
                                            token,
                                            &floatFeatures[featuresLayout.GetInternalFeatureIdx(featureId)]
                                         ))
                                    {
                                        CB_ENSURE(
                                            false,
                                            "Factor " << featureId << " cannot be parsed as float."
                                            " Try correcting column description file."
                                        );
                                    }
                                }
                                ++featureId;
                                break;
                            }
                            case EColumn::Label: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for Label");
                                visitor->AddTarget(lineIdx, TString(token));
                                break;
                            }
                            case EColumn::Weight: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for weight");
                                visitor->AddWeight(lineIdx, FromString<float>(token));
                                break;
                            }
                            case EColumn::Auxiliary: {
                                break;
                            }
                            case EColumn::GroupId: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for GroupId");
                                visitor->AddGroupId(lineIdx, CalcGroupIdFor(token));
                                break;
                            }
                            case EColumn::GroupWeight: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for GroupWeight");
                                visitor->AddGroupWeight(lineIdx, FromString<float>(token));
                                break;
                            }
                            case EColumn::SubgroupId: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for SubgroupId");
                                visitor->AddSubgroupId(lineIdx, CalcSubgroupIdFor(token));
                                break;
                            }
                            case EColumn::Baseline: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for Baseline");
                                visitor->AddBaseline(lineIdx, baselineIdx, FromString<float>(token));
                                ++baselineIdx;
                                break;
                            }
                            case EColumn::DocId: {
                                break;
                            }
                            case EColumn::Timestamp: {
                                CB_ENSURE(token.length() != 0, "empty values not supported for Timestamp");
                                visitor->AddTimestamp(lineIdx, FromString<ui64>(token));
                                break;
                            }
                            default: {
                                CB_ENSURE(false, "wrong column type");
                            }
                        }
                    } catch (yexception& e) {
                        throw TCatBoostException() << "Column " << tokenCount << " (type "
                            << columnsDescription[tokenCount].Type << ", value = \"" << token
                            << "\"): " << e.what();
                    }
                    ++tokenCount;
                }
                if (!floatFeatures.empty()) {
                    visitor->AddAllFloatFeatures(lineIdx, floatFeatures);
                }
                if (!catFeatures.empty()) {
                    visitor->AddAllCatFeatures(lineIdx, catFeatures);
                }
                CB_ENSURE(
                    tokenCount == columnsDescription.size(),
                    "wrong columns number: expected " << columnsDescription.ysize()
                    << ", found " << tokenCount
                );
            } catch (yexception& e) {
                throw TCatBoostException() << "Error in dsv data. Line " <<
                    AsyncRowProcessor.GetLinesProcessed() + lineIdx + 1 << ": " << e.what();
            }
        };

        AsyncRowProcessor.ProcessBlock(parseBlock);
    }

    namespace {
        TDatasetLoaderFactory::TRegistrator<TCBDsvDataLoader> DefDataLoaderReg("");
        TDatasetLoaderFactory::TRegistrator<TCBDsvDataLoader> CBDsvDataLoaderReg("dsv");
    }
}

