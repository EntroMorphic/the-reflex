"""
Setup script for reflex-cli.
"""

from setuptools import setup, find_packages

setup(
    name="reflex-cli",
    version="0.1.0",
    description="The Reflex Deployment Tool - Deploy, validate, and monitor CNS on ESP32",
    author="EntroMorphic Research",
    packages=find_packages(),
    python_requires=">=3.8",
    install_requires=[
        "typer>=0.9.0",
        "pyserial>=3.5",
        "esptool>=4.0",
        "pyyaml>=6.0",
    ],
    entry_points={
        "console_scripts": [
            "reflex-cli=reflex_cli.__main__:main",
        ],
    },
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
    ],
)
