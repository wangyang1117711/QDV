#ifndef UNDO_MANAGER_H
#define UNDO_MANAGER_H

#include <QUndoStack>
#include <QObject>
#include <memory>

class UndoManager : public QObject {
    Q_OBJECT
    
public:
    static UndoManager* instance();
    
    QUndoStack* stack() const { return m_stack.get(); }
    
    void push(QUndoCommand* command);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    
signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoTextChanged(const QString& text);
    void redoTextChanged(const QString& text);
    
private:
    UndoManager(QObject* parent = nullptr);
    ~UndoManager() override;
    
    std::unique_ptr<QUndoStack> m_stack;
    
    static UndoManager* s_instance;
    static QMutex s_mutex;
};

class ToolAddCommand : public QUndoCommand {
public:
    ToolAddCommand(class Scheme* scheme, class VisionTool* tool, QUndoCommand* parent = nullptr);
    ~ToolAddCommand() override;
    
    void undo() override;
    void redo() override;
    
private:
    class Scheme* m_scheme;
    class VisionTool* m_tool;
    bool m_ownsTool;
};

class ToolRemoveCommand : public QUndoCommand {
public:
    ToolRemoveCommand(class Scheme* scheme, const QString& toolId, QUndoCommand* parent = nullptr);
    ~ToolRemoveCommand() override;
    
    void undo() override;
    void redo() override;
    
private:
    class Scheme* m_scheme;
    QString m_toolId;
    class VisionTool* m_tool;
};

class ToolMoveCommand : public QUndoCommand {
public:
    ToolMoveCommand(class Scheme* scheme, int fromIndex, int toIndex, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;
    
private:
    class Scheme* m_scheme;
    int m_fromIndex;
    int m_toIndex;
};

#endif // UNDO_MANAGER_H