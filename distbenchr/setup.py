from setuptools import setup

setup(name='distbenchr',
        version='0.1',
        description='A library for running distributed benchmarks',
        author='Marios Kogias',
        author_email='marios.kogias@epfl.ch',
        license='MIT',
        packages=['distbenchr'],
        python_requires='>=2.7, <3',
        install_requires=[
            'fabric<2.0'
        ],
        zip_safe=False)
