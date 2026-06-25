#include <QtWidgets>
#include <QApplication>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QFontDatabase>
#include <QPainter>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QShortcut>
#include <QKeySequence>
#include <QFileInfo>
#include <QTextBlock>
#include <QResizeEvent>

class CodeEditor;

class FlatTextEditorStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override {
        if (element == PE_Frame || element == PE_PanelLineEdit) {
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(QWidget *parent = nullptr)
        : QWidget(parent), m_editor(nullptr) {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setEditor(CodeEditor *editor) { m_editor = editor; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodeEditor *m_editor;
};

class CodeEditor : public QPlainTextEdit {
public:
    explicit CodeEditor(QWidget *parent = nullptr)
        : QPlainTextEdit(parent),
          m_lineNumberArea(new LineNumberArea(this)) {
        m_lineNumberArea->setEditor(this);

        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(10);
        QPlainTextEdit::setFont(mono);

        setFrameStyle(QFrame::NoFrame);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setTabStopDistance(20);

        connect(this, &QPlainTextEdit::blockCountChanged,
                this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest,
                this, &CodeEditor::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged,
                this, &CodeEditor::highlightCurrentLine);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
    }

    void setEditorFont(const QFont &f) {
        QPlainTextEdit::setFont(f);
        updateLineNumberAreaWidth(0);
        m_lineNumberArea->update();
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }

        // Add extra padding for better spacing
        int padding = 10;
        int space = padding + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + padding;
        return space;
    }

    void setSearchSelections(const QList<QTextEdit::ExtraSelection> &selections) {
        m_searchSelections = selections;
        applyExtraSelections();
    }

    void clearSearchSelections() {
        m_searchSelections.clear();
        applyExtraSelections();
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event) {
        QPainter painter(m_lineNumberArea);

        const QColor gutterBg = palette().color(QPalette::Window).darker(108);
        painter.fillRect(event->rect(), gutterBg);

        // Draw divider line at the right edge with some padding
        painter.setPen(palette().color(QPalette::Mid));
        int dividerX = m_lineNumberArea->width() - 1;
        painter.drawLine(dividerX, 0, dividerX, m_lineNumberArea->height());

        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();

        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        // Add padding from the right edge
        int rightPadding = 10;
        int numberAreaWidth = m_lineNumberArea->width() - rightPadding;

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                QString number = QString::number(blockNumber + 1);
                painter.drawText(0, top, numberAreaWidth, fontMetrics().height(),
                                 Qt::AlignRight | Qt::AlignVCenter, number);
            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        updateLineNumberAreaWidth(0);
    }

private slots:
    void updateLineNumberAreaWidth(int) {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
        m_lineNumberArea->setGeometry(QRect(0, 0, lineNumberAreaWidth(), height()));
    }

    void updateLineNumberArea(const QRect &rect, int dy) {
        if (dy) {
            m_lineNumberArea->scroll(0, dy);
        } else {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }

        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth(0);
        }
    }

    void highlightCurrentLine() {
        QColor lineColor = palette().color(QPalette::AlternateBase).lighter(115);

        m_currentLineSelection.format.setBackground(lineColor);
        m_currentLineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
        m_currentLineSelection.cursor = textCursor();
        m_currentLineSelection.cursor.clearSelection();

        applyExtraSelections();
    }

    void applyExtraSelections() {
        QList<QTextEdit::ExtraSelection> selections;
        selections.append(m_currentLineSelection);
        selections.append(m_searchSelections);
        setExtraSelections(selections);
    }

    LineNumberArea *m_lineNumberArea;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QTextEdit::ExtraSelection m_currentLineSelection;

    friend class LineNumberArea;
};

QSize LineNumberArea::sizeHint() const {
    if (!m_editor) {
        return QSize(50, 0);
    }
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    if (m_editor) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
}

class SearchWidget : public QWidget {
public:
    explicit SearchWidget(CodeEditor *editor, QWidget *parent = nullptr)
        : QWidget(parent), m_editor(editor), m_highlighting(true) {
        setVisible(false);
        setStyleSheet("background-color: palette(window); border-bottom: 0.5px solid palette(mid);");

        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 5, 10, 5);
        layout->setSpacing(8);

        QLabel *findLabel = new QLabel("Find:", this);
        m_searchInput = new QLineEdit(this);
        m_searchInput->setPlaceholderText("Search...");
        m_searchInput->setFixedWidth(200);

        QPushButton *findNextBtn = new QPushButton("Next", this);
        QPushButton *findPrevBtn = new QPushButton("Prev", this);
        QPushButton *highlightBtn = new QPushButton("Highlight All", this);
        highlightBtn->setCheckable(true);
        highlightBtn->setChecked(true);

        QPushButton *closeBtn = new QPushButton("✕", this);
        closeBtn->setFixedSize(24, 24);
        closeBtn->setStyleSheet(
            "QPushButton { border: none; }"
            "QPushButton:hover { background-color: palette(midlight); }"
        );

        layout->addWidget(findLabel);
        layout->addWidget(m_searchInput);
        layout->addWidget(findNextBtn);
        layout->addWidget(findPrevBtn);
        layout->addWidget(highlightBtn);
        layout->addStretch();
        layout->addWidget(closeBtn);

        connect(m_searchInput, &QLineEdit::textChanged, [this](const QString &text) {
            clearHighlights();
            if (text.isEmpty()) return;
            if (m_highlighting) {
                highlightAllMatches(text);
            }
        });

        connect(m_searchInput, &QLineEdit::returnPressed, [this]() {
            findNext();
        });

        connect(findNextBtn, &QPushButton::clicked, [this]() {
            findNext();
        });

        connect(findPrevBtn, &QPushButton::clicked, [this]() {
            findPrev();
        });

        connect(highlightBtn, &QPushButton::toggled, [this](bool enabled) {
            m_highlighting = enabled;
            clearHighlights();

            const QString text = m_searchInput->text();
            if (enabled && !text.isEmpty()) {
                highlightAllMatches(text);
            }
        });

        connect(closeBtn, &QPushButton::clicked, [this]() {
            setVisible(false);
            clearHighlights();
            m_editor->setFocus();
        });

        QShortcut *searchShortcut = new QShortcut(QKeySequence::Find, m_editor);
        connect(searchShortcut, &QShortcut::activated, [this]() {
            setVisible(true);
            m_searchInput->setFocus();
            m_searchInput->selectAll();
        });

        QShortcut *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(escapeShortcut, &QShortcut::activated, [this]() {
            if (isVisible()) {
                setVisible(false);
                clearHighlights();
                m_editor->setFocus();
            }
        });
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            setVisible(false);
            clearHighlights();
            m_editor->setFocus();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void findNext() {
        const QString searchText = m_searchInput->text();
        if (searchText.isEmpty()) return;

        QTextCursor cursor = m_editor->textCursor();
        QTextCursor found = m_editor->document()->find(searchText, cursor);

        if (found.isNull()) {
            QTextCursor startCursor(m_editor->document());
            startCursor.movePosition(QTextCursor::Start);
            found = m_editor->document()->find(searchText, startCursor);
        }

        if (!found.isNull()) {
            m_editor->setTextCursor(found);
            m_editor->ensureCursorVisible();
        }
    }

    void findPrev() {
        const QString searchText = m_searchInput->text();
        if (searchText.isEmpty()) return;

        QTextCursor cursor = m_editor->textCursor();
        QTextCursor found = m_editor->document()->find(searchText, cursor, QTextDocument::FindBackward);

        if (found.isNull()) {
            QTextCursor endCursor(m_editor->document());
            endCursor.movePosition(QTextCursor::End);
            found = m_editor->document()->find(searchText, endCursor, QTextDocument::FindBackward);
        }

        if (!found.isNull()) {
            m_editor->setTextCursor(found);
            m_editor->ensureCursorVisible();
        }
    }

    void highlightAllMatches(const QString &searchText) {
        QList<QTextEdit::ExtraSelection> selections;
        QTextCursor cursor(m_editor->document());

        QTextCharFormat format;
        format.setBackground(QColor(255, 255, 0, 110));

        cursor = m_editor->document()->find(searchText, cursor);
        while (!cursor.isNull()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor;
            sel.format = format;
            selections.append(sel);
            cursor = m_editor->document()->find(searchText, cursor);
        }

        m_editor->setSearchSelections(selections);
    }

    void clearHighlights() {
        m_editor->clearSearchSelections();
    }

    CodeEditor *m_editor;
    QLineEdit *m_searchInput;
    bool m_highlighting;
};

class TextEditorWindow : public QMainWindow {
public:
    TextEditorWindow(QWidget *parent = nullptr)
        : QMainWindow(parent),
          m_currentFile(""),
          m_fontSize(10),
          m_minFontSize(6),
          m_maxFontSize(60) { // Increased from 23 to 26 (3 more zoom levels)
        QWidget *central = new QWidget(this);
        setCentralWidget(central);

        QVBoxLayout *mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // Menu Bar
        QMenuBar *menuBar = new QMenuBar(this);
        QMenu *fileMenu = menuBar->addMenu("File");
        QAction *newAction = fileMenu->addAction("New");
        QAction *openAction = fileMenu->addAction("Open");
        QAction *saveAction = fileMenu->addAction("Save");
        QAction *saveAsAction = fileMenu->addAction("Save As");
        fileMenu->addSeparator();
        QAction *exitAction = fileMenu->addAction("Exit");

        QMenu *editMenu = menuBar->addMenu("Edit");
        QAction *undoAction = editMenu->addAction("Undo");
        QAction *redoAction = editMenu->addAction("Redo");
        editMenu->addSeparator();
        QAction *cutAction = editMenu->addAction("Cut");
        QAction *copyAction = editMenu->addAction("Copy");
        QAction *pasteAction = editMenu->addAction("Paste");
        editMenu->addSeparator();
        QAction *findAction = editMenu->addAction("Find");
        findAction->setShortcut(QKeySequence::Find);

        QMenu *viewMenu = menuBar->addMenu("View");
        QAction *zoomInAction = viewMenu->addAction("Zoom In");
        zoomInAction->setShortcut(QKeySequence("Ctrl+="));
        QAction *zoomOutAction = viewMenu->addAction("Zoom Out");
        zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));
        QAction *resetZoomAction = viewMenu->addAction("Reset Zoom");
        resetZoomAction->setShortcut(QKeySequence("Ctrl+0"));

        setMenuBar(menuBar);

        // Toolbar
        QWidget *toolBar = new QWidget(this);
        toolBar->setFixedHeight(34);
        toolBar->setStyleSheet("background-color: palette(window); border-bottom: 0.5px solid palette(mid);");

        QHBoxLayout *toolLayout = new QHBoxLayout(toolBar);
        toolLayout->setContentsMargins(12, 0, 12, 0);
        toolLayout->setSpacing(14);

        QPushButton *newBtn = createToolButton("New", QStyle::SP_FileDialogNewFolder);
        QPushButton *openBtn = createToolButton("Open", QStyle::SP_DirOpenIcon);
        QPushButton *saveBtn = createToolButton("Save", QStyle::SP_DriveFDIcon);
        toolLayout->addWidget(newBtn);
        toolLayout->addWidget(openBtn);
        toolLayout->addWidget(saveBtn);
        addSeparator(toolLayout);

        QPushButton *undoBtn = createToolButton("Undo", QStyle::SP_ArrowBack);
        QPushButton *redoBtn = createToolButton("Redo", QStyle::SP_ArrowForward);
        toolLayout->addWidget(undoBtn);
        toolLayout->addWidget(redoBtn);
        addSeparator(toolLayout);

        QPushButton *cutBtn = createToolButton("Cut", QStyle::SP_FileDialogToParent);
        QPushButton *copyBtn = createToolButton("Copy", QStyle::SP_FileDialogContentsView);
        QPushButton *pasteBtn = createToolButton("Paste", QStyle::SP_FileDialogDetailedView);
        toolLayout->addWidget(cutBtn);
        toolLayout->addWidget(copyBtn);
        toolLayout->addWidget(pasteBtn);
        addSeparator(toolLayout);

        QPushButton *findBtn = createToolButton("Find", QStyle::SP_FileDialogStart);
        QPushButton *wrapBtn = createToolButton("Word Wrap", QStyle::SP_FileDialogEnd);
        wrapBtn->setCheckable(true);
        toolLayout->addWidget(findBtn);
        toolLayout->addWidget(wrapBtn);
        toolLayout->addStretch();

        QLabel *zoomLabel = new QLabel("Zoom:", this);
        QPushButton *zoomOutBtn = createToolButton("Zoom Out", QStyle::SP_TitleBarMinButton);
        QPushButton *zoomInBtn = createToolButton("Zoom In", QStyle::SP_TitleBarMaxButton);
        QPushButton *resetZoomBtn = createToolButton("Reset Zoom", QStyle::SP_BrowserReload);
        toolLayout->addWidget(zoomLabel);
        toolLayout->addWidget(zoomOutBtn);
        toolLayout->addWidget(zoomInBtn);
        toolLayout->addWidget(resetZoomBtn);

        // Editor
        m_editor = new CodeEditor(this);
        m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_font.setPointSize(m_fontSize);
        m_editor->setEditorFont(m_font);
        m_editor->setStyleSheet("QPlainTextEdit { border: none; background: palette(base); }");

        m_searchWidget = new SearchWidget(m_editor, this);

        QWidget *editorArea = new QWidget(this);
        QVBoxLayout *editorAreaLayout = new QVBoxLayout(editorArea);
        editorAreaLayout->setContentsMargins(0, 0, 0, 0);
        editorAreaLayout->setSpacing(0);
        editorAreaLayout->addWidget(m_searchWidget);
        editorAreaLayout->addWidget(m_editor);

        // Status Bar
        QStatusBar *statusBar = new QStatusBar(this);
        statusBar->setStyleSheet("QStatusBar { background-color: palette(window); border-top: 0.5px solid palette(mid); }");
        statusBar->setFixedHeight(24);

        m_cursorLabel = new QLabel("Ln 1, Col 1", this);
        m_selLabel = new QLabel("", this);
        m_wordLabel = new QLabel("0 words", this);
        m_encodingLabel = new QLabel("UTF-8", this);
        m_lineEndingLabel = new QLabel("LF", this);
        m_modeLabel = new QLabel("Plain Text", this);
        m_zoomLabel = new QLabel("100%", this);

        statusBar->addWidget(m_cursorLabel);
        statusBar->addWidget(m_selLabel);
        statusBar->addPermanentWidget(m_wordLabel);
        statusBar->addPermanentWidget(m_encodingLabel);
        statusBar->addPermanentWidget(m_lineEndingLabel);
        statusBar->addPermanentWidget(m_modeLabel);
        statusBar->addPermanentWidget(m_zoomLabel);

        setStatusBar(statusBar);

        // Layout
        mainLayout->addWidget(menuBar);
        mainLayout->addWidget(toolBar);
        mainLayout->addWidget(editorArea);

        // Connections
        connect(newAction, &QAction::triggered, this, &TextEditorWindow::newFile);
        connect(openAction, &QAction::triggered, this, &TextEditorWindow::openFile);
        connect(saveAction, &QAction::triggered, this, &TextEditorWindow::saveFile);
        connect(saveAsAction, &QAction::triggered, this, &TextEditorWindow::saveFileAs);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);

        connect(undoAction, &QAction::triggered, m_editor, &QPlainTextEdit::undo);
        connect(redoAction, &QAction::triggered, m_editor, &QPlainTextEdit::redo);
        connect(cutAction, &QAction::triggered, m_editor, &QPlainTextEdit::cut);
        connect(copyAction, &QAction::triggered, m_editor, &QPlainTextEdit::copy);
        connect(pasteAction, &QAction::triggered, m_editor, &QPlainTextEdit::paste);

        connect(findAction, &QAction::triggered, [this]() {
            m_searchWidget->setVisible(true);
            m_searchWidget->setFocus();
        });

        connect(zoomInAction, &QAction::triggered, [this]() { zoomIn(); });
        connect(zoomOutAction, &QAction::triggered, [this]() { zoomOut(); });
        connect(resetZoomAction, &QAction::triggered, [this]() { resetZoom(); });

        connect(newBtn, &QPushButton::clicked, this, &TextEditorWindow::newFile);
        connect(openBtn, &QPushButton::clicked, this, &TextEditorWindow::openFile);
        connect(saveBtn, &QPushButton::clicked, this, &TextEditorWindow::saveFile);

        connect(undoBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::undo);
        connect(redoBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::redo);
        connect(cutBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::cut);
        connect(copyBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::copy);
        connect(pasteBtn, &QPushButton::clicked, m_editor, &QPlainTextEdit::paste);

        connect(findBtn, &QPushButton::clicked, [this]() {
            m_searchWidget->setVisible(true);
            m_searchWidget->setFocus();
        });

        connect(wrapBtn, &QPushButton::toggled, [this](bool checked) {
            m_editor->setLineWrapMode(checked ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        });

        connect(zoomInBtn, &QPushButton::clicked, [this]() { zoomIn(); });
        connect(zoomOutBtn, &QPushButton::clicked, [this]() { zoomOut(); });
        connect(resetZoomBtn, &QPushButton::clicked, [this]() { resetZoom(); });

        connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
                this, &TextEditorWindow::updateStatusBar);
        connect(m_editor, &QPlainTextEdit::textChanged,
                this, &TextEditorWindow::updateWordCount);

        updateStatusBar();
        updateWordCount();
        updateZoomLabel();

        setWindowTitle("Untitled - Text Editor");
        resize(800, 560);
    }

private:
    QPushButton *createToolButton(const QString &tooltip, QStyle::StandardPixmap icon) {
        QPushButton *btn = new QPushButton(this);
        btn->setToolTip(tooltip);
        btn->setFlat(true);
        btn->setStyleSheet(
            "QPushButton { border: none; color: palette(text); padding: 4px; }"
            "QPushButton:hover { background-color: palette(midlight); }"
            "QPushButton:checked { background-color: palette(midlight); }"
        );
        btn->setFixedSize(24, 24);
        btn->setIcon(style()->standardIcon(icon));
        btn->setIconSize(QSize(17, 17));
        return btn;
    }

    void addSeparator(QHBoxLayout *layout) {
        QFrame *sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setStyleSheet("background-color: palette(mid); max-width: 0.5px; min-width: 0.5px;");
        sep->setFixedHeight(16);
        layout->addWidget(sep);
    }

    void zoomIn() {
        if (m_fontSize < m_maxFontSize) {
            m_fontSize += 1;
            m_font.setPointSize(m_fontSize);
            m_editor->setEditorFont(m_font);
            updateZoomLabel();
        }
    }

    void zoomOut() {
        if (m_fontSize > m_minFontSize) {
            m_fontSize -= 1;
            m_font.setPointSize(m_fontSize);
            m_editor->setEditorFont(m_font);
            updateZoomLabel();
        }
    }

    void resetZoom() {
        m_fontSize = 10;
        m_font.setPointSize(m_fontSize);
        m_editor->setEditorFont(m_font);
        updateZoomLabel();
    }

    void updateZoomLabel() {
        int percent = qRound((m_fontSize / 10.0) * 100);
        m_zoomLabel->setText(QString("%1%").arg(percent));
    }

    void newFile() {
        if (maybeSave()) {
            m_editor->clear();
            m_currentFile.clear();
            setWindowTitle("Untitled - Text Editor");
        }
    }

    void openFile() {
        if (!maybeSave()) return;

        QString fileName = QFileDialog::getOpenFileName(
            this, "Open File", "", "Text Files (*.txt);;All Files (*)"
        );
        if (fileName.isEmpty()) return;

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Cannot open file: " + file.errorString());
            return;
        }

        QTextStream in(&file);
        m_editor->setPlainText(in.readAll());
        m_currentFile = fileName;
        setWindowTitle(QFileInfo(fileName).fileName() + " - Text Editor");
    }

    bool saveFile() {
        if (m_currentFile.isEmpty()) {
            return saveFileAs();
        }
        return saveFileTo(m_currentFile);
    }

    bool saveFileAs() {
        QString fileName = QFileDialog::getSaveFileName(
            this, "Save File", "", "Text Files (*.txt);;All Files (*)"
        );
        if (fileName.isEmpty()) return false;
        return saveFileTo(fileName);
    }

    bool saveFileTo(const QString &fileName) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Cannot save file: " + file.errorString());
            return false;
        }

        QTextStream out(&file);
        out << m_editor->toPlainText();
        m_currentFile = fileName;
        setWindowTitle(QFileInfo(fileName).fileName() + " - Text Editor");
        return true;
    }

    bool maybeSave() {
        if (m_editor->document()->isModified()) {
            QMessageBox::StandardButton ret = QMessageBox::warning(
                this,
                "Unsaved Changes",
                "The document has been modified.\nDo you want to save your changes?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
            );

            if (ret == QMessageBox::Save) {
                return saveFile();
            } else if (ret == QMessageBox::Cancel) {
                return false;
            }
        }
        return true;
    }

    void updateStatusBar() {
        QTextCursor cursor = m_editor->textCursor();
        int block = cursor.blockNumber() + 1;
        int col = cursor.columnNumber() + 1;
        m_cursorLabel->setText(QString("Ln %1, Col %2").arg(block).arg(col));

        int selLen = cursor.selectedText().length();
        m_selLabel->setText(selLen > 0 ? QString("(%1 selected)").arg(selLen) : "");
    }

    void updateWordCount() {
        QString text = m_editor->toPlainText();
        int words = 0;
        if (!text.trimmed().isEmpty()) {
            words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
        }
        m_wordLabel->setText(QString("%1 word%2").arg(words).arg(words != 1 ? "s" : ""));
    }

    CodeEditor *m_editor;
    SearchWidget *m_searchWidget;
    QLabel *m_cursorLabel;
    QLabel *m_selLabel;
    QLabel *m_wordLabel;
    QLabel *m_encodingLabel;
    QLabel *m_lineEndingLabel;
    QLabel *m_modeLabel;
    QLabel *m_zoomLabel;
    QString m_currentFile;
    int m_fontSize;
    int m_minFontSize;
    int m_maxFontSize;
    QFont m_font;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle(new FlatTextEditorStyle("fusion"));

    TextEditorWindow window;
    window.show();

    return app.exec();
}
