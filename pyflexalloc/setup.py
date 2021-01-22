# -*- coding: utf-8 -*-
from setuptools import setup
import pkg_resources

packages = \
['flexalloc', 'flexalloc.pyutils']

package_data = \
{'': ['*']}

with open("requirements.txt") as fh:
    install_requires = [str(e) for e in pkg_resources.parse_requirements(fh.read())]

setup_kwargs = {
    'name': 'flexalloc',
    'version': '0.1.0',
    'description': '',
    'long_description': None,
    'author': 'Jesper Wendel Devantier',
    'author_email': 'j.devantier@samsung.com',
    'maintainer': None,
    'maintainer_email': None,
    'url': None,
    'packages': packages,
    'package_data': package_data,
    'install_requires': install_requires,
    'python_requires': '>=3.8,<4.0',
}
from build_hook import *
build(setup_kwargs)

setup(**setup_kwargs)
