[![Build status](https://circleci.com/gh/direct-code-execution/ns-3-dce.svg?style=shield&circle-token=a9cf0c7e5e7a1d1a7ff2e2e5b341706fba3ccfb2)](https://circleci.com/gh/direct-code-execution/ns-3-dce)
[![Documentation Status](https://readthedocs.org/projects/ns-3-dce/badge/?version=latest)](http://ns-3-dce.readthedocs.io/en/latest/?badge=latest)
[![License](https://img.shields.io/badge/license-GPL-brightgreen.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

What is Direct Code Execution (DCE) ?
===

Direct Code Execution (DCE) is a framework for ns-3 that provides facilities to execute, within ns-3, existing implementations of userspace and kernelspace network protocols or applications without source code changes. For example, instead of using ns-3’s implementation of a ping-like application, you can use the real ping application. You can also use the Linux networking stack in simulations.


More explanation available on:
https://www.nsnam.org/about/projects/direct-code-execution/

In this fork, we mainly fix dce-v1.12 for 5G-NR and Wi-Fi MPTCP simulation, which works under ubuntu 20.04. More info available on: https://github.com/wonderfulnx/ns-3-mptcp-sim-bundle

Documentation
===

In case you have a question, you should first refer to the documentation:

* A manual is available at https://ns-3-dce.readthedocs.io/en/latest/

And look for answers on the ns3 mailing list https://groups.google.com/forum/#!forum/ns-3-users.

Contribute
===
Development is done on github and bugs should be filed at https://github.com/direct-code-execution/ns-3-dce/issues.

The mailing list used to discuss internals is the same as ns3's: https://groups.google.com/g/ns-developers

License
===
DCE is available under the GPLv2 license. A copy is available in the repository.


# Similar projects:

- [Network Simulation Cradle](https://www.nsnam.org/wiki/Network_Simulation_Cradle_Integration) (Not maintained anymore)
- [VMSimInt](http://eudl.eu/doi/10.4108/icst.simutools.2014.254623)

