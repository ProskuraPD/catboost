

PYTEST()

TEST_SRCS(
    test.py
)

DEPENDS(
    catboost/tools/limited_precision_dsv_diff
)

FORK_SUBTESTS()
FORK_TEST_FILES()
SPLIT_FACTOR(20)

SIZE(MEDIUM)
REQUIREMENTS(network:full)

PEERDIR(
    catboost/pytest/lib
    catboost/python-package/lib
    contrib/python/numpy
)

DEPENDS(
    catboost/app
)

DATA(
    arcadia/catboost/pytest/data
)

END()

IF(HAVE_CUDA)
    RECURSE(
    cuda_tests
)
ENDIF()
