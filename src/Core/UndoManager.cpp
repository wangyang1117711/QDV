#include "UndoManager.h"
#include "Scheme.h"
#include "VisionTool.h"
#include <QMutexLocker>
#include <QApplication>

UndoManager* UndoManager::s_instance = nullptr;
QMutex UndoManager::s_mutex;

UndoManager* UndoManager::instance() {
    if (!s_instance) {
        QMutexLocker locker(&s_mutex);
        if (!s_instance) {
            s_instance = new UndoManager();
        }
    }
    return s_instance;
}

UndoManager::UndoManager(QObject* parent) : QObject(parent) {
    m_stack = std::make_unique<QUndoStack>();
    
    connect(m_stack.get(), &QUndoStack::canUndoChanged, this, &UndoManager::canUndoChanged);
    connect(m_stack.get(), &QUndoStack::canRedoChanged, this, &UndoManager::canRedoChanged);
    connect(m_stack.get(), &QUndoStack::undoTextChanged, this, &UndoManager::undoTextChanged);
    connect(m_stack.get(), &QUndoStack::redoTextChanged, this, &UndoManager::redoTextChanged);
}

UndoManager::~UndoManager() = default;

void UndoManager::push(QUndoCommand* command) {
    m_stack->push(command);
}

void UndoManager::undo() {
    m_stack->undo();
}

void UndoManager::redo() {
    m_stack->redo();
}

bool UndoManager::canUndo() const {
    return m_stack->canUndo();
}

bool UndoManager::canRedo() const {
    return m_stack->canRedo();
}

QString UndoManager::undoText() const {
    return m_stack->undoText();
}

QString UndoManager::redoText() const {
    return m_stack->redoText();
}

ToolAddCommand::ToolAddCommand(Scheme* scheme, VisionTool* tool, QUndoCommand* parent)
    : QUndoCommand(parent), m_scheme(scheme), m_tool(tool), m_ownsTool(true) {
    setText(QString("Add tool: %1").arg(tool->name()));
}

ToolAddCommand::~ToolAddCommand() {
    if (m_ownsTool && m_tool) {
        delete m_tool;
    }
}

void ToolAddCommand::undo() {
    if (m_scheme && m_tool) {
        m_scheme->removeTool(m_tool->id());
        m_ownsTool = true;
    }
}

void ToolAddCommand::redo() {
    if (m_scheme && m_tool) {
        m_scheme->addTool(m_tool);
        m_ownsTool = false;
    }
}

ToolRemoveCommand::ToolRemoveCommand(Scheme* scheme, const QString& toolId, QUndoCommand* parent)
    : QUndoCommand(parent), m_scheme(scheme), m_toolId(toolId), m_tool(nullptr) {
    VisionTool* tool = scheme->getTool(toolId);
    if (tool) {
        setText(QString("Remove tool: %1").arg(tool->name()));
    }
}

ToolRemoveCommand::~ToolRemoveCommand() {
    if (m_tool) {
        delete m_tool;
    }
}

void ToolRemoveCommand::undo() {
    if (m_scheme && m_tool) {
        m_scheme->addTool(m_tool);
        m_tool = nullptr;
    }
}

void ToolRemoveCommand::redo() {
    if (m_scheme) {
        m_tool = m_scheme->getTool(m_toolId);
        if (m_tool) {
            m_scheme->removeTool(m_toolId);
        }
    }
}

ToolMoveCommand::ToolMoveCommand(Scheme* scheme, int fromIndex, int toIndex, QUndoCommand* parent)
    : QUndoCommand(parent), m_scheme(scheme), m_fromIndex(fromIndex), m_toIndex(toIndex) {
    setText("Reorder tools");
}

void ToolMoveCommand::undo() {
    if (m_scheme) {
        m_scheme->moveTool(m_toIndex, m_fromIndex);
    }
}

void ToolMoveCommand::redo() {
    if (m_scheme) {
        m_scheme->moveTool(m_fromIndex, m_toIndex);
    }
}