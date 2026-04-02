# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Common Commands

### Starting the Application

```bash
# Start both frontend and backend together
python scripts/start_web.py

# Or start separately:
# Backend (FastAPI)
python src/api/run_server.py
# or: uvicorn src.api.main:app --host 0.0.0.0 --port 8001 --reload

# Frontend (Next.js)
cd web && npm run dev
```

### Installation and Dependencies

```bash
# One-click installation (recommended)
python scripts/install_all.py
# Or: bash scripts/install_all.sh

# Manual installation
pip install -r requirements.txt
npm install --prefix web
```

### Testing

```bash
# Run all tests
pytest

# Run specific test file
pytest tests/test_task_parser.py
pytest tests/test_project_router.py

# Run tests with coverage
pytest --cov=src/agents/project
```

### Configuration

```bash
# Copy environment template and configure
cp .env.example .env
# Edit .env with your API keys and settings
```

## Architecture Overview

DeepTutor is an AI-powered learning system with a **modular agent-based architecture**:

```
┌─────────────────────────────────────────────────────────────────┐
│                        FastAPI Backend                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │  Agent      │  │   Tools     │  │  Services   │      │
│  │  Modules    │→ │   Layer     │→ │  Layer      │      │
│  └─────────────┘  └─────────────┘  └─────────────┘      │
│        ↓                    ↓                    ↓            │
│  ┌─────────────────────────────────────────────────────┐     │
│  │              RAG + Knowledge Graph                 │     │
│  └─────────────────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Next.js Frontend                      │
│  React 19 + Tailwind CSS + WebSocket Streaming              │
└─────────────────────────────────────────────────────────────────┘
```

### Backend Structure

**`src/agents/`** - Agent implementations for each module:
- **solve**: Dual-loop problem solving (Analysis Loop + Solve Loop)
- **question**: Custom and exam-mimicking question generation
- **research**: Deep research with dynamic topic queue
- **guide**: Interactive guided learning
- **co_writer**: AI-assisted Markdown editing
- **ideagen**: Research idea generation
- **project**: AI-powered project scaffolding (newly added)

**`src/tools/`** - Tool layer for agent operations:
- `rag_tool.py`: RAG hybrid/naive retrieval
- `code_executor.py`: Python code execution
- `web_search.py`: Web search (Perplexity, Tavily, etc.)
- `query_item_tool.py`: Knowledge graph entity lookup
- `paper_search_tool.py`: Academic paper search

**`src/services/`** - Service layer:
- `llm/`: LLM factory (supports OpenAI, Anthropic, DashScope, etc.)
- `rag/`: RAG pipeline management
- `embedding/`: Embedding service
- `search/`: Search service integration

**`src/api/`** - FastAPI application:
- `routers/`: REST and WebSocket endpoints per module
- `main.py`: App setup with CORS, static file serving
- WebSocket endpoints provide real-time streaming for long-running operations

### Frontend Structure

**`web/app/`** - Next.js 16 pages:
- `page.tsx`: Dashboard
- Module pages: `/solver`, `/question`, `/research`, `/guide`, `/co_writer`, `/ideagen`, `/project`

**`web/components/`** - React components
**`web/context/`** - Global state management (`GlobalContext.tsx`)

### Configuration Hierarchy

1. **Environment Variables** (`.env`) - API keys, endpoints, ports
2. **`config/agents.yaml`** - LLM parameters (temperature, max_tokens) per module
3. **`config/main.yaml`** - System settings, paths, tool configs

### Key Architectural Patterns

**WebSocket Streaming**: All long-running operations use WebSocket for real-time progress updates:
```python
@router.websocket("/module/operation")
async def websocket_handler(websocket: WebSocket):
    await websocket.accept()
    # Stream updates: {"type": "status", "content": "..."}
```

**Agent-Based**: Each module has specialized agents:
- Solve: InvestigateAgent, NoteAgent, PlanAgent, ManagerAgent, SolveAgent, CheckAgent
- Research: RephraseAgent, DecomposeAgent, ManagerAgent, ResearchAgent, NoteAgent

**Dual-Loop Architecture** (Smart Solver):
1. **Analysis Loop**: InvestigateAgent → NoteAgent (context gathering)
2. **Solve Loop**: PlanAgent → ManagerAgent → SolveAgent → CheckAgent (solution generation)

**Knowledge Graph + Vector Store**: LightRAG provides entity-relation mapping for semantic connections

### Data Storage

All user content stored in `data/`:
```
data/
├── knowledge_bases/    # RAG knowledge bases
└── user/              # User activity data
    ├── solve/          # Problem solving results
    ├── question/       # Generated questions
    ├── research/        # Research reports
    ├── guide/          # Guided learning sessions
    ├── co-writer/      # Co-writer documents
    └── notebook/       # Notebook records
```

### Important Notes

1. **LLM Parameters**: Always load from `config/agents.yaml` using `get_agent_params(module_name)`. Never hardcode.
2. **Model Names**: Set via environment variables only (`.env`), not config files.
3. **WebSocket URLs**: Production must use `wss://`, development uses `ws://`.
4. **Code Execution**: Restricted to `allowed_roots` in `config/main.yaml` for security.
5. **Remote Access**: Set `NEXT_PUBLIC_API_BASE=` in `.env` when accessing from another device.
6. **Claude Code CLI**: Project Creator module requires `claude` CLI installed and authenticated (OAuth, not API key).

### Adding New Features

1. Create agent in `src/agents/new_module/`
2. Add router in `src/api/routers/new_module.py`
3. Register router in `src/api/main.py`
4. Add page in `web/app/new_module/page.tsx`
5. Update `web/components/Sidebar.tsx` navigation
6. Add LLM params to `config/agents.yaml` (temperature, max_tokens)
7. Add module settings to `config/main.yaml`
