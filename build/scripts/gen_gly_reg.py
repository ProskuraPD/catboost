import sys
import argparse

TEMPLATE = '''
{includes}
#include <glycine/gen/runtime/lib/cpp_wrapper.h>
#include <glycine/gen/runtime/lib/python_wrapper.h>
#include <glycine/gen/runtime/lib/registry.h>

static const NGlycine::TRegHelper REG(
    "{name}",
    {wrapper}
);
'''

PY_WRAPPER = 'new NGlycine::TPythonWrapper("{impl}")'
CPP_WRAPPER = 'new NGlycine::TCppWrapper<{impl}>()'


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("name")
    parser.add_argument("output")
    parser.add_argument("includes", nargs="*")

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--py")
    group.add_argument("--cpp")

    return parser.parse_args()


if __name__ == '__main__':
    args = parse_args()

    includes = "\n".join(
        "#include <{}>".format(include)
        for include in args.includes
    )

    if args.py:
        wrapper = PY_WRAPPER.format(impl=args.py)
    elif args.cpp:
        wrapper = CPP_WRAPPER.format(impl=args.cpp)

    code = TEMPLATE.format(
        includes=includes,
        name=args.name,
        wrapper=wrapper,
    )

    with open(args.output, "w") as f:
        f.write(code)
