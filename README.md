# <u>N</u>OELLE <u>O</u>ffers <u>E</u>mpowering <u>LL</u>VM <u>E</u>xtensions


## Table of Contents
- [Description](#description)
- [Version](#version)
- [Prerequisites](#prerequisites)
- [Building NOELLE](#building-noelle)
- [Uninstalling NOELLE](#uninstalling-noelle)
- [Testing NOELLE](#testing-noelle)
- [Repository structure](#repository-structure)
- [Examples of using NOELLE](#examples-of-using-noelle)
- [Contributions](#contributions)
- [License](#license)


## Description
NOELLE provides abstractions to help build advanced code analyses and transformations for LLVM IR code.
GINO is built upon [SVF](https://svf-tools.github.io/SVF/), [SCAF](https://github.com/PrincetonUniversity/SCAF.git), and [LLVM](http://llvm.org).

NOELLE is in active development so more tools, tests, and abstractions will be added.

We release NOELLE's source code in the hope others will benefit from it.
You are kindly asked to acknowledge usage of the tool by citing the following paper:
```
@inproceedings{NOELLE,
    title={{NOELLE} {O}ffers {E}mpowering {LL}VM {E}xtensions},
    author={Angelo Matni and Enrico Armenio Deiana and Yian Su and Lukas Gross and Souradip Ghosh and Sotiris Apostolakis and Ziyang Xu and Zujun Tan and Ishita Chaturvedi and David I. August and Simone Campanoni},
    booktitle={International Symposium on Code Generation and Optimization, 2022. CGO 2022.},
    year={2022}
}
```

The only documentation available for NOELLE is:
- a [video](https://www.youtube.com/watch?v=whORNUUWIjI&t=7s) introducing NOELLE
- the [paper](http://www.cs.northwestern.edu/~simonec/files/Research/papers/HELIX_CGO_2022.pdf)
- the comments within the code
- the slides we use in the class [Advanced Topics in Compilers](http://www.cs.northwestern.edu/~simonec/ATC.html)
- [the wiki](https://github.com/arcana-lab/noelle/wiki) of the project

## Version
The latest stable version is 9.14.0 (tag = `v9.14.0`).

### Version Numbering Scheme
The version number is in the form of \[v _Major.Minor.Revision_ \]
- **Major**: Each major version matches a specific LLVM version (e.g., version 9 matches LLVM 9, version 11 matches LLVM 11)
- **Minor**: Starts from 0, each minor version represents either one or more API replacements/removals that might impact the users OR a forced update every six months (the minimum minor update frequency)
- **Revision**: Starts from 0; each revision version may include bug fixes or incremental improvements

#### Update Frequency
- **Major**: Matches the LLVM releases on a best-effort basis
- **Minor**: At least once per six months, at most once per month (1/month ~ 2/year)
- **Revision**: At least once per month, at most twice per week (2/week ~ 1/month)


## Prerequisites
LLVM 9.0.0

### Northwestern
Next is the information for those who have access to the Zythos cluster at Northwestern.

To enable the correct LLVM, run the following command from any node of the Zythos cluster:
```
source /project/extra/llvm/9.0.0/enable
```

The guide about the Zythos cluster can be downloaded [here](http://www.cs.northwestern.edu/~simonec/files/Research/manuals/Zythos_guide.pdf).


## Building NOELLE
To build and install NOELLE: run `make` from the repository root directory.

## Uninstalling NOELLE

Run `make clean` from the root directory to clean the repository.

Run `make uninstall` from the root directory to uninstall the NOELLE installation.


## Testing NOELLE
To run all tests, invoke the following commands:
```
make clean ; 
cd tests ;
make ;
```

## Repository structure
The directory `src` includes sources of the noelle framework.
Within this directory, `src/core` includes the abstractions provided by NOELLE.
Also, `src/tools` includes code transformations that rely on the NOELLE's abstractions to modify the code.

The directory `external` includes libraries that are external to NOELLE that are used by NOELLE.
Some of these libraries are patched and/or extended for NOELLE.

The directory `tests` includes unit tests.

The directory `examples` includes examples of LLVM passes (and their tests) that rely on the NOELLE framework.

Finally, the directory `doc` includes the documentation of NOELLE.


## Examples of using NOELLE
LLVM passes in the directory `examples/passes` shows use cases of NOELLE.

If you have any trouble using this framework feel free to reach out to us for help (contact simone.campanoni@northwestern.edu).

### Contributing to NOELLE
NOELLE uses `clang-format` to ensure uniform styling across the project's source code.
`clang-format` is run automatically as a pre-commit git hook, meaning that when you commit a file `clang-format` is automatically run on the file in place.

Since git doesn't allow for git hooks to be installed when you clone the repository we manage this with our top-level Makefile.
To install the NOELLE git hooks, run `make hooks` at the root of the directory.
This make rule is run at the start of the `make all` rule as well for ease of use.


## Contributions
We welcome contributions from the community to improve this framework and evolve it to cater to more users.


## License
NOELLE is licensed under the [MIT License](./LICENSE.md).
