#include "train_template_pointwise.h"
#include <catboost/cuda/targets/pointwise_target_impl.h>

namespace NCatboostCuda {
    using TPointwiseTrainer = TGpuTrainer<TPointwiseTargetsImpl>;

    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorPoisson(ELossFunction::Poisson);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorMape(ELossFunction::MAPE);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorMae(ELossFunction::MAE);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorQuantile(ELossFunction::Quantile);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorLogLinQuantile(ELossFunction::LogLinQuantile);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorRMSE(ELossFunction::RMSE);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorLogloss(ELossFunction::Logloss);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorCrossEntropy(ELossFunction::CrossEntropy);
    TGpuTrainerFactory::TRegistrator<TPointwiseTrainer> RegistratorLq(ELossFunction::Lq);

}
