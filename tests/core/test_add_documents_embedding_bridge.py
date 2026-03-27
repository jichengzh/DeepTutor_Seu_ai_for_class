#!/usr/bin/env python
# -*- coding: utf-8 -*-

import asyncio
import importlib.util
import logging
import os
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


def _load_add_documents_module():
    """Load src/knowledge/add_documents.py with lightweight stubs."""
    module_path = Path(__file__).resolve().parents[2] / "src" / "knowledge" / "add_documents.py"

    # Stub external dependencies used at import-time in add_documents.py
    dotenv_mod = types.ModuleType("dotenv")
    dotenv_mod.load_dotenv = lambda *args, **kwargs: None
    sys.modules.setdefault("dotenv", dotenv_mod)

    src_mod = sys.modules.setdefault("src", types.ModuleType("src"))

    knowledge_pkg = sys.modules.setdefault("src.knowledge", types.ModuleType("src.knowledge"))
    setattr(src_mod, "knowledge", knowledge_pkg)
    extract_mod = types.ModuleType("src.knowledge.extract_numbered_items")
    extract_mod.process_content_list = lambda *args, **kwargs: None
    sys.modules.setdefault("src.knowledge.extract_numbered_items", extract_mod)

    logging_mod = types.ModuleType("src.logging")

    class _DummyLogCtx:
        def __init__(self, *args, **kwargs):
            pass

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc, tb):
            return False

    logging_mod.LightRAGLogContext = _DummyLogCtx
    logging_mod.get_logger = lambda name: logging.getLogger(name)
    sys.modules.setdefault("src.logging", logging_mod)

    services_pkg = sys.modules.setdefault("src.services", types.ModuleType("src.services"))
    setattr(src_mod, "services", services_pkg)

    llm_mod = types.ModuleType("src.services.llm")
    llm_mod.get_llm_config = lambda: types.SimpleNamespace(api_key="", base_url="")
    sys.modules.setdefault("src.services.llm", llm_mod)

    llm_cfg_mod = types.ModuleType("src.services.llm.config")
    llm_cfg_mod.get_llm_config = llm_mod.get_llm_config
    sys.modules.setdefault("src.services.llm.config", llm_cfg_mod)

    spec = importlib.util.spec_from_file_location("add_documents_for_test", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


_ADD_DOC_MODULE = _load_add_documents_module()
DocumentAdder = _ADD_DOC_MODULE.DocumentAdder
DEFAULT_EMBEDDING_BRIDGE_MODE = _ADD_DOC_MODULE.DEFAULT_EMBEDDING_BRIDGE_MODE
EMBEDDING_BRIDGE_MODE_ENV = _ADD_DOC_MODULE.EMBEDDING_BRIDGE_MODE_ENV


class _DummyEmbeddingConfig:
    dim = 1024
    max_tokens = 8192


class _DummyEmbeddingClient:
    async def embed(self, texts):
        return [[float(i)] for i, _ in enumerate(texts)]

    def get_embedding_func(self):
        return "CLIENT_WRAPPER_SENTINEL"


class _FakeEmbeddingFunc:
    def __init__(self, embedding_dim, max_token_size, func):
        self.embedding_dim = embedding_dim
        self.max_token_size = max_token_size
        self.func = func


class TestAddDocumentsEmbeddingBridgeMode(unittest.TestCase):
    def setUp(self):
        self.adder = DocumentAdder.__new__(DocumentAdder)
        self.client = _DummyEmbeddingClient()
        self.cfg = _DummyEmbeddingConfig()

    def test_default_mode_is_legacy_when_env_missing(self):
        with patch.dict(os.environ, {}, clear=False):
            os.environ.pop(EMBEDDING_BRIDGE_MODE_ENV, None)
            mode = self.adder._get_embedding_bridge_mode()
            self.assertEqual(mode, DEFAULT_EMBEDDING_BRIDGE_MODE)

    def test_invalid_mode_falls_back_to_legacy(self):
        with patch.dict(os.environ, {EMBEDDING_BRIDGE_MODE_ENV: "bad_mode"}, clear=False):
            mode = self.adder._get_embedding_bridge_mode()
            self.assertEqual(mode, DEFAULT_EMBEDDING_BRIDGE_MODE)

    def test_client_wrapper_mode_uses_embedding_client_wrapper(self):
        with patch.dict(os.environ, {EMBEDDING_BRIDGE_MODE_ENV: "client_wrapper"}, clear=False):
            embedding_func = self.adder._build_embedding_func_for_lightrag(
                embedding_client=self.client,
                embedding_cfg=self.cfg,
                embedding_func_cls=_FakeEmbeddingFunc,
            )
            self.assertEqual(embedding_func, "CLIENT_WRAPPER_SENTINEL")

    def test_legacy_mode_builds_legacy_embedding_func(self):
        with patch.dict(os.environ, {EMBEDDING_BRIDGE_MODE_ENV: "legacy"}, clear=False):
            embedding_func = self.adder._build_embedding_func_for_lightrag(
                embedding_client=self.client,
                embedding_cfg=self.cfg,
                embedding_func_cls=_FakeEmbeddingFunc,
            )

            self.assertIsInstance(embedding_func, _FakeEmbeddingFunc)
            self.assertEqual(embedding_func.embedding_dim, 1024)
            self.assertEqual(embedding_func.max_token_size, 8192)

            vectors = asyncio.run(embedding_func.func(["a", "b"]))
            self.assertIsInstance(vectors, list)
            self.assertEqual(len(vectors), 2)


if __name__ == "__main__":
    unittest.main()
