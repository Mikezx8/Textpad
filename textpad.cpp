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
#include <QSettings>
#include <QAction>
#include <QTabWidget>
#include <QTabBar>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>

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
          m_lineNumberArea(new LineNumberArea(this)),
          m_darkMode(false),
          m_showLineNumbers(false) {
        m_lineNumberArea->setEditor(this);

        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(10);
        QPlainTextEdit::setFont(mono);

        setFrameStyle(QFrame::NoFrame);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setTabStopDistance(20);
        setAcceptDrops(true);

        connect(this, &QPlainTextEdit::blockCountChanged,
                this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest,
                this, &CodeEditor::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged,
                this, &CodeEditor::highlightCurrentLine);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
        setLineNumbersVisible(false);
    }

    void setEditorFont(const QFont &f) {
        QPlainTextEdit::setFont(f);
        updateLineNumberAreaWidth(0);
        m_lineNumberArea->update();
        highlightCurrentLine();
    }

    void setLineNumbersVisible(bool visible) {
        m_showLineNumbers = visible;
        if (visible) {
            setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
            m_lineNumberArea->setGeometry(QRect(0, 0, lineNumberAreaWidth(), height()));
            m_lineNumberArea->show();
        } else {
            setViewportMargins(0, 0, 0, 0);
            m_lineNumberArea->hide();
        }
        m_lineNumberArea->update();
    }

    bool lineNumbersVisible() const {
        return m_showLineNumbers;
    }

    int lineNumberAreaWidth() const {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }

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

    void setDarkMode(bool dark) {
        m_darkMode = dark;
        highlightCurrentLine();
        m_lineNumberArea->update();
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event) {
        QPainter painter(m_lineNumberArea);

        QColor gutterBg;
        if (m_darkMode) {
            gutterBg = QColor(37, 37, 38);
        } else {
            gutterBg = palette().color(QPalette::Window).darker(108);
        }
        painter.fillRect(event->rect(), gutterBg);

        painter.setPen(palette().color(QPalette::Mid));
        int dividerX = m_lineNumberArea->width() - 1;
        painter.drawLine(dividerX, 0, dividerX, m_lineNumberArea->height());

        painter.setFont(font());
        
        QColor textColor;
        if (m_darkMode) {
            textColor = QColor(187, 187, 187);
        } else {
            textColor = palette().color(QPalette::WindowText);
        }
        painter.setPen(textColor);

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();

        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

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
        if (m_showLineNumbers) {
            updateLineNumberAreaWidth(0);
        }
    }
    
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }
    
    void dropEvent(QDropEvent *event) override {
        const QMimeData *mimeData = event->mimeData();
        if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();
            for (const QUrl &url : urls) {
                if (url.isLocalFile()) {
                    QString filePath = url.toLocalFile();
                    fileDropped(filePath);
                }
            }
            event->acceptProposedAction();
        }
    }

private slots:
    void updateLineNumberAreaWidth(int) {
        if (m_showLineNumbers) {
            setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
            m_lineNumberArea->setGeometry(QRect(0, 0, lineNumberAreaWidth(), height()));
        }
    }

    void updateLineNumberArea(const QRect &rect, int dy) {
        if (!m_showLineNumbers) return;
        
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
        QColor lineColor;
        if (m_darkMode) {
            lineColor = QColor(60, 60, 65);
        } else {
            lineColor = palette().color(QPalette::AlternateBase).lighter(115);
        }

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

private:
    LineNumberArea *m_lineNumberArea;
    QList<QTextEdit::ExtraSelection> m_searchSelections;
    QTextEdit::ExtraSelection m_currentLineSelection;
    bool m_darkMode;
    bool m_showLineNumbers;
    friend class LineNumberArea;

    void fileDropped(const QString &filePath) {
        Q_UNUSED(filePath);
    }
};

QSize LineNumberArea::sizeHint() const {
    if (!m_editor || !m_editor->lineNumbersVisible()) {
        return QSize(0, 0);
    }
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    if (m_editor && m_editor->lineNumbersVisible()) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
}

class SearchWidget : public QWidget {
public:
    explicit SearchWidget(CodeEditor *editor, QWidget *parent = nullptr)
        : QWidget(parent), m_editor(editor), m_highlighting(true) {
        setVisible(false);
        setupUI();
        
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

        connect(m_findNextBtn, &QPushButton::clicked, [this]() {
            findNext();
        });

        connect(m_findPrevBtn, &QPushButton::clicked, [this]() {
            findPrev();
        });

        connect(m_highlightBtn, &QPushButton::toggled, [this](bool enabled) {
            m_highlighting = enabled;
            clearHighlights();

            const QString text = m_searchInput->text();
            if (enabled && !text.isEmpty()) {
                highlightAllMatches(text);
            }
        });

        connect(m_closeBtn, &QPushButton::clicked, [this]() {
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
        
        applyTheme(false);
    }

    void applyTheme(bool dark) {
        if (dark) {
            setStyleSheet(
                "QWidget { background-color: #2b2b2b; color: #ffffff; }"
                "QLineEdit { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; }"
                "QPushButton { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; }"
                "QPushButton:hover { background-color: #4a4a4a; }"
                "QLabel { color: #ffffff; }"
            );
        } else {
            setStyleSheet("");
        }
    }

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
    void setupUI() {
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 5, 10, 5);
        layout->setSpacing(8);

        QLabel *findLabel = new QLabel("Find:", this);
        m_searchInput = new QLineEdit(this);
        m_searchInput->setPlaceholderText("Search...");
        m_searchInput->setFixedWidth(200);

        m_findNextBtn = new QPushButton("Next", this);
        m_findPrevBtn = new QPushButton("Prev", this);
        m_highlightBtn = new QPushButton("Highlight All", this);
        m_highlightBtn->setCheckable(true);
        m_highlightBtn->setChecked(true);

        m_closeBtn = new QPushButton("✕", this);
        m_closeBtn->setFixedSize(24, 24);
        m_closeBtn->setStyleSheet(
            "QPushButton { border: none; }"
            "QPushButton:hover { background-color: palette(midlight); }"
        );

        layout->addWidget(findLabel);
        layout->addWidget(m_searchInput);
        layout->addWidget(m_findNextBtn);
        layout->addWidget(m_findPrevBtn);
        layout->addWidget(m_highlightBtn);
        layout->addStretch();
        layout->addWidget(m_closeBtn);
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
    QPushButton *m_findNextBtn;
    QPushButton *m_findPrevBtn;
    QPushButton *m_highlightBtn;
    QPushButton *m_closeBtn;
    bool m_highlighting;
};

class EditorTab : public QWidget {
public:
    CodeEditor *editor;
    QString filePath;
    QString fileName;
    bool modified;
    
    EditorTab(QWidget *parent = nullptr) : QWidget(parent) {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        
        editor = new CodeEditor(this);
        layout->addWidget(editor);
        
        modified = false;
        filePath = "";
        fileName = "Untitled";
        
        connect(editor, &QPlainTextEdit::modificationChanged, [this](bool changed) {
            modified = changed;
            updateTabTitle();
        });
    }
    
    void updateTabTitle() {
        QString title = fileName.isEmpty() ? "Untitled" : fileName;
        if (modified) {
            title += " *";
        }
        QTabWidget *tabWidget = dynamic_cast<QTabWidget*>(parent());
        if (tabWidget) {
            int index = tabWidget->indexOf(this);
            if (index >= 0) {
                tabWidget->setTabText(index, title);
            }
        }
    }
};

class TextEditorWindow : public QMainWindow {
public:
    TextEditorWindow(QWidget *parent = nullptr)
        : QMainWindow(parent),
          m_fontSize(10),
          m_minFontSize(6),
          m_maxFontSize(60) {
        loadSettings();
        setupUI();
        applyTheme(m_darkMode);
        setupKeyboardShortcuts();
        createNewTab();
    }

    ~TextEditorWindow() {
        saveSettings();
    }
    
    void openFileInCurrentTab(const QString &filePath) {
        if (currentEditor()) {
            loadFileIntoEditor(currentTab(), filePath);
        }
    }
    
    EditorTab* createNewTab(const QString &content = QString(), const QString &filePath = QString()) {
        EditorTab *tab = new EditorTab(m_tabWidget);
        
        if (!filePath.isEmpty()) {
            tab->filePath = filePath;
            tab->fileName = QFileInfo(filePath).fileName();
            if (!content.isEmpty()) {
                tab->editor->setPlainText(content);
            }
        } else {
            tab->fileName = getUniqueUntitledName();
        }
        
        SearchWidget *searchWidget = new SearchWidget(tab->editor, tab);
        searchWidget->setVisible(false);
        
        connect(tab->editor, &QPlainTextEdit::cursorPositionChanged,
                this, &TextEditorWindow::updateStatusBar);
        connect(tab->editor, &QPlainTextEdit::textChanged,
                this, &TextEditorWindow::updateWordCount);
        connect(tab->editor, &QPlainTextEdit::modificationChanged,
                this, &TextEditorWindow::updateWindowTitle);
        
        tab->editor->setLineNumbersVisible(m_showLineNumbers);
        tab->editor->setDarkMode(m_darkMode);
        tab->editor->setEditorFont(m_font);
        
        // Add the tab with the correct name
        int index = m_tabWidget->addTab(tab, tab->fileName);
        m_tabWidget->setCurrentIndex(index);
        
        updateTabBarVisibility();
        updateWindowTitle();
        return tab;
    }

private:
    QString getUniqueUntitledName() {
        int untitledCount = 1;
        QString baseName = "Untitled";
        while (true) {
            QString name = baseName;
            if (untitledCount > 1) {
                name += QString("-%1").arg(untitledCount);
            }
            bool exists = false;
            for (int i = 0; i < m_tabWidget->count(); i++) {
                EditorTab *existing = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
                if (existing && existing->fileName == name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                return name;
            }
            untitledCount++;
        }
    }

    void setupUI() {
        QWidget *central = new QWidget(this);
        setCentralWidget(central);

        QVBoxLayout *mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QMenuBar *menuBar = new QMenuBar(this);
        QMenu *fileMenu = menuBar->addMenu("File");
        QAction *newAction = fileMenu->addAction("New");
        newAction->setShortcut(QKeySequence::New);
        QAction *openAction = fileMenu->addAction("Open");
        openAction->setShortcut(QKeySequence::Open);
        QAction *saveAction = fileMenu->addAction("Save");
        saveAction->setShortcut(QKeySequence::Save);
        QAction *saveAsAction = fileMenu->addAction("Save As");
        saveAsAction->setShortcut(QKeySequence::SaveAs);
        QAction *saveAllAction = fileMenu->addAction("Save All");
        saveAllAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
        fileMenu->addSeparator();
        QAction *closeTabAction = fileMenu->addAction("Close Tab");
        closeTabAction->setShortcut(QKeySequence("Ctrl+W"));
        QAction *closeAllTabsAction = fileMenu->addAction("Close All Tabs");
        closeAllTabsAction->setShortcut(QKeySequence("Ctrl+Shift+W"));
        fileMenu->addSeparator();
        QAction *exitAction = fileMenu->addAction("Exit");
        exitAction->setShortcut(QKeySequence::Quit);

        QMenu *editMenu = menuBar->addMenu("Edit");
        QAction *undoAction = editMenu->addAction("Undo");
        undoAction->setShortcut(QKeySequence::Undo);
        QAction *redoAction = editMenu->addAction("Redo");
        redoAction->setShortcut(QKeySequence::Redo);
        editMenu->addSeparator();
        QAction *cutAction = editMenu->addAction("Cut");
        cutAction->setShortcut(QKeySequence::Cut);
        QAction *copyAction = editMenu->addAction("Copy");
        copyAction->setShortcut(QKeySequence::Copy);
        QAction *pasteAction = editMenu->addAction("Paste");
        pasteAction->setShortcut(QKeySequence::Paste);
        editMenu->addSeparator();
        QAction *findAction = editMenu->addAction("Find");
        findAction->setShortcut(QKeySequence::Find);
        QAction *findNextAction = editMenu->addAction("Find Next");
        findNextAction->setShortcut(QKeySequence::FindNext);
        QAction *findPreviousAction = editMenu->addAction("Find Previous");
        findPreviousAction->setShortcut(QKeySequence::FindPrevious);

        QMenu *viewMenu = menuBar->addMenu("View");
        QAction *zoomInAction = viewMenu->addAction("Zoom In");
        zoomInAction->setShortcut(QKeySequence("Ctrl+="));
        QAction *zoomOutAction = viewMenu->addAction("Zoom Out");
        zoomOutAction->setShortcut(QKeySequence("Ctrl+-"));
        QAction *resetZoomAction = viewMenu->addAction("Reset Zoom");
        resetZoomAction->setShortcut(QKeySequence("Ctrl+0"));

        QMenu *settingsMenu = menuBar->addMenu("Settings");
        m_darkModeAction = settingsMenu->addAction("Dark Mode");
        m_darkModeAction->setCheckable(true);
        m_darkModeAction->setChecked(m_darkMode);
        settingsMenu->addSeparator();
        m_showLineNumbersAction = settingsMenu->addAction("Show Line Numbers");
        m_showLineNumbersAction->setCheckable(true);
        m_showLineNumbersAction->setChecked(m_showLineNumbers);

        setMenuBar(menuBar);

        QWidget *toolBar = new QWidget(this);
        toolBar->setFixedHeight(34);
        toolBar->setObjectName("toolBar");

        QHBoxLayout *toolLayout = new QHBoxLayout(toolBar);
        toolLayout->setContentsMargins(12, 0, 12, 0);
        toolLayout->setSpacing(14);

        QPushButton *newBtn = createToolButton("New", QStyle::SP_FileDialogNewFolder);
        QPushButton *openBtn = createToolButton("Open", QStyle::SP_DirOpenIcon);
        QPushButton *saveBtn = createToolButton("Save", QStyle::SP_DriveFDIcon);
        QPushButton *saveAllBtn = createToolButton("Save All", QStyle::SP_DriveCDIcon);
        toolLayout->addWidget(newBtn);
        toolLayout->addWidget(openBtn);
        toolLayout->addWidget(saveBtn);
        toolLayout->addWidget(saveAllBtn);
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
        m_themeBtn = createToolButton("Toggle Theme", QStyle::SP_DialogApplyButton);
        toolLayout->addWidget(zoomLabel);
        toolLayout->addWidget(zoomOutBtn);
        toolLayout->addWidget(zoomInBtn);
        toolLayout->addWidget(resetZoomBtn);
        toolLayout->addWidget(m_themeBtn);

        m_tabWidget = new QTabWidget(this);
        m_tabWidget->setTabsClosable(true);
        m_tabWidget->setMovable(true);
        m_tabWidget->setDocumentMode(true);
        m_tabWidget->tabBar()->setExpanding(false);
        m_tabWidget->tabBar()->setVisible(false);
        
        connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &TextEditorWindow::closeTab);
        connect(m_tabWidget, &QTabWidget::currentChanged, this, &TextEditorWindow::onTabChanged);

        QStatusBar *statusBar = new QStatusBar(this);
        statusBar->setObjectName("statusBar");
        statusBar->setFixedHeight(24);

        m_cursorLabel = new QLabel("Ln 1, Col 1", this);
        m_selLabel = new QLabel("", this);
        m_wordLabel = new QLabel("0 words", this);
        m_encodingLabel = new QLabel("UTF-8", this);
        m_lineEndingLabel = new QLabel("LF", this);
        m_modeLabel = new QLabel("Plain Text", this);
        m_zoomLabel = new QLabel("100%", this);
        m_fileLabel = new QLabel("Untitled", this);

        statusBar->addWidget(m_cursorLabel);
        statusBar->addWidget(m_selLabel);
        statusBar->addPermanentWidget(m_fileLabel);
        statusBar->addPermanentWidget(m_wordLabel);
        statusBar->addPermanentWidget(m_encodingLabel);
        statusBar->addPermanentWidget(m_lineEndingLabel);
        statusBar->addPermanentWidget(m_modeLabel);
        statusBar->addPermanentWidget(m_zoomLabel);

        setStatusBar(statusBar);

        mainLayout->addWidget(menuBar);
        mainLayout->addWidget(toolBar);
        mainLayout->addWidget(m_tabWidget);

        connect(newAction, &QAction::triggered, this, &TextEditorWindow::newFile);
        connect(openAction, &QAction::triggered, this, &TextEditorWindow::openFile);
        connect(saveAction, &QAction::triggered, this, &TextEditorWindow::saveCurrentFile);
        connect(saveAsAction, &QAction::triggered, this, &TextEditorWindow::saveCurrentFileAs);
        connect(saveAllAction, &QAction::triggered, this, &TextEditorWindow::saveAllFiles);
        connect(closeTabAction, &QAction::triggered, this, &TextEditorWindow::closeCurrentTab);
        connect(closeAllTabsAction, &QAction::triggered, this, &TextEditorWindow::closeAllTabs);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);

        connect(undoAction, &QAction::triggered, [this]() { if (currentEditor()) currentEditor()->undo(); });
        connect(redoAction, &QAction::triggered, [this]() { if (currentEditor()) currentEditor()->redo(); });
        connect(cutAction, &QAction::triggered, [this]() { if (currentEditor()) currentEditor()->cut(); });
        connect(copyAction, &QAction::triggered, [this]() { if (currentEditor()) currentEditor()->copy(); });
        connect(pasteAction, &QAction::triggered, [this]() { if (currentEditor()) currentEditor()->paste(); });

        connect(findAction, &QAction::triggered, [this]() {
            if (currentEditor()) {
                QWidget *currentTab = m_tabWidget->currentWidget();
                if (currentTab) {
                    EditorTab *tab = dynamic_cast<EditorTab*>(currentTab);
                    if (tab && tab->editor) {
                        QList<SearchWidget*> searchWidgets = tab->editor->findChildren<SearchWidget*>();
                        if (!searchWidgets.isEmpty()) {
                            searchWidgets.first()->setVisible(true);
                            searchWidgets.first()->setFocus();
                        }
                    }
                }
            }
        });
        
        connect(findNextAction, &QAction::triggered, [this]() {
            if (currentEditor()) {
                QWidget *currentTab = m_tabWidget->currentWidget();
                if (currentTab) {
                    EditorTab *tab = dynamic_cast<EditorTab*>(currentTab);
                    if (tab && tab->editor) {
                        QList<SearchWidget*> searchWidgets = tab->editor->findChildren<SearchWidget*>();
                        if (!searchWidgets.isEmpty()) {
                            searchWidgets.first()->findNext();
                        }
                    }
                }
            }
        });
        
        connect(findPreviousAction, &QAction::triggered, [this]() {
            if (currentEditor()) {
                QWidget *currentTab = m_tabWidget->currentWidget();
                if (currentTab) {
                    EditorTab *tab = dynamic_cast<EditorTab*>(currentTab);
                    if (tab && tab->editor) {
                        QList<SearchWidget*> searchWidgets = tab->editor->findChildren<SearchWidget*>();
                        if (!searchWidgets.isEmpty()) {
                            searchWidgets.first()->findPrev();
                        }
                    }
                }
            }
        });

        connect(zoomInAction, &QAction::triggered, [this]() { zoomIn(); });
        connect(zoomOutAction, &QAction::triggered, [this]() { zoomOut(); });
        connect(resetZoomAction, &QAction::triggered, [this]() { resetZoom(); });

        connect(newBtn, &QPushButton::clicked, this, &TextEditorWindow::newFile);
        connect(openBtn, &QPushButton::clicked, this, &TextEditorWindow::openFile);
        connect(saveBtn, &QPushButton::clicked, this, &TextEditorWindow::saveCurrentFile);
        connect(saveAllBtn, &QPushButton::clicked, this, &TextEditorWindow::saveAllFiles);

        connect(undoBtn, &QPushButton::clicked, [this]() { if (currentEditor()) currentEditor()->undo(); });
        connect(redoBtn, &QPushButton::clicked, [this]() { if (currentEditor()) currentEditor()->redo(); });
        connect(cutBtn, &QPushButton::clicked, [this]() { if (currentEditor()) currentEditor()->cut(); });
        connect(copyBtn, &QPushButton::clicked, [this]() { if (currentEditor()) currentEditor()->copy(); });
        connect(pasteBtn, &QPushButton::clicked, [this]() { if (currentEditor()) currentEditor()->paste(); });

        connect(findBtn, &QPushButton::clicked, [this]() {
            if (currentEditor()) {
                QWidget *currentTab = m_tabWidget->currentWidget();
                if (currentTab) {
                    EditorTab *tab = dynamic_cast<EditorTab*>(currentTab);
                    if (tab && tab->editor) {
                        QList<SearchWidget*> searchWidgets = tab->editor->findChildren<SearchWidget*>();
                        if (!searchWidgets.isEmpty()) {
                            searchWidgets.first()->setVisible(true);
                            searchWidgets.first()->setFocus();
                        }
                    }
                }
            }
        });

        connect(wrapBtn, &QPushButton::toggled, [this](bool checked) {
            if (currentEditor()) {
                currentEditor()->setLineWrapMode(checked ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
            }
        });

        connect(zoomInBtn, &QPushButton::clicked, [this]() { zoomIn(); });
        connect(zoomOutBtn, &QPushButton::clicked, [this]() { zoomOut(); });
        connect(resetZoomBtn, &QPushButton::clicked, [this]() { resetZoom(); });
        connect(m_themeBtn, &QPushButton::clicked, [this]() { toggleTheme(); });
        
        connect(m_darkModeAction, &QAction::toggled, [this](bool checked) {
            m_darkMode = checked;
            applyTheme(checked);
            saveSettings();
        });
        
        connect(m_showLineNumbersAction, &QAction::toggled, [this](bool checked) {
            m_showLineNumbers = checked;
            for (int i = 0; i < m_tabWidget->count(); i++) {
                EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
                if (tab && tab->editor) {
                    tab->editor->setLineNumbersVisible(checked);
                }
            }
            saveSettings();
        });

        updateStatusBar();
        updateWordCount();
        updateZoomLabel();

        resize(800, 560);
    }

    void setupKeyboardShortcuts() {
        QShortcut *closeTabShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
        connect(closeTabShortcut, &QShortcut::activated, this, &TextEditorWindow::closeCurrentTab);
        
        QShortcut *closeAllShortcut = new QShortcut(QKeySequence("Ctrl+Shift+W"), this);
        connect(closeAllShortcut, &QShortcut::activated, this, &TextEditorWindow::closeAllTabs);
        
        QShortcut *nextTabShortcut = new QShortcut(QKeySequence("Ctrl+Tab"), this);
        connect(nextTabShortcut, &QShortcut::activated, [this]() {
            int current = m_tabWidget->currentIndex();
            int next = (current + 1) % m_tabWidget->count();
            m_tabWidget->setCurrentIndex(next);
        });
        
        QShortcut *prevTabShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
        connect(prevTabShortcut, &QShortcut::activated, [this]() {
            int current = m_tabWidget->currentIndex();
            int prev = (current - 1 + m_tabWidget->count()) % m_tabWidget->count();
            m_tabWidget->setCurrentIndex(prev);
        });
    }

    CodeEditor* currentEditor() {
        EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->currentWidget());
        return tab ? tab->editor : nullptr;
    }

    EditorTab* currentTab() {
        return dynamic_cast<EditorTab*>(m_tabWidget->currentWidget());
    }

    void updateTabBarVisibility() {
        m_tabWidget->tabBar()->setVisible(m_tabWidget->count() > 1);
    }

    void closeTab(int index) {
        EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(index));
        if (!tab) return;
        
        if (tab->editor->document()->isModified()) {
            QMessageBox::StandardButton ret = QMessageBox::warning(
                this,
                "Unsaved Changes",
                QString("'%1' has been modified.\nDo you want to save your changes?").arg(tab->fileName),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
            );

            if (ret == QMessageBox::Save) {
                if (!saveFile(tab)) {
                    return;
                }
            } else if (ret == QMessageBox::Cancel) {
                return;
            }
        }
        
        m_tabWidget->removeTab(index);
        tab->deleteLater();
        
        if (m_tabWidget->count() == 0) {
            createNewTab();
        }
        
        updateTabBarVisibility();
        updateWindowTitle();
    }

    void closeCurrentTab() {
        int index = m_tabWidget->currentIndex();
        if (index >= 0) {
            closeTab(index);
        }
    }

    void closeAllTabs() {
        while (m_tabWidget->count() > 0) {
            closeTab(0);
        }
    }

    void onTabChanged(int index) {
        Q_UNUSED(index);
        updateStatusBar();
        updateWordCount();
        updateWindowTitle();
    }

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

    void loadSettings() {
        QSettings settings("TextEditor", "Settings");
        m_darkMode = settings.value("darkMode", false).toBool();
        m_fontSize = settings.value("fontSize", 10).toInt();
        m_showLineNumbers = settings.value("showLineNumbers", false).toBool();
    }

    void saveSettings() {
        QSettings settings("TextEditor", "Settings");
        settings.setValue("darkMode", m_darkMode);
        settings.setValue("fontSize", m_fontSize);
        settings.setValue("showLineNumbers", m_showLineNumbers);
    }

    void applyTheme(bool dark) {
        if (m_tabWidget) {
            for (int i = 0; i < m_tabWidget->count(); i++) {
                EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
                if (tab && tab->editor) {
                    tab->editor->setDarkMode(dark);
                    QList<SearchWidget*> searchWidgets = tab->editor->findChildren<SearchWidget*>();
                    for (SearchWidget *sw : searchWidgets) {
                        sw->applyTheme(dark);
                    }
                }
            }
        }

        if (dark) {
            QString darkStyle = 
                "QWidget { background-color: #2b2b2b; color: #ffffff; }"
                "QMainWindow { background-color: #2b2b2b; }"
                "QMenuBar { background-color: #3c3c3c; color: #ffffff; }"
                "QMenuBar::item { background-color: transparent; }"
                "QMenuBar::item:selected { background-color: #4a4a4a; }"
                "QMenu { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; }"
                "QMenu::item { background-color: transparent; }"
                "QMenu::item:selected { background-color: #4a4a4a; }"
                "QMenu::separator { background-color: #555; height: 1px; }"
                "QStatusBar { background-color: #3c3c3c; color: #ffffff; }"
                "QLabel { color: #ffffff; }"
                "QLineEdit { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; padding: 2px; }"
                "QLineEdit:focus { border: 1px solid #0078d4; }"
                "QPushButton { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; padding: 4px 8px; border-radius: 2px; }"
                "QPushButton:hover { background-color: #4a4a4a; }"
                "QPushButton:pressed { background-color: #555; }"
                "QPushButton:checked { background-color: #4a4a4a; }"
                "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"
                "QTabWidget::pane { background-color: #2b2b2b; border: none; }"
                "QTabBar::tab { background-color: #3c3c3c; color: #ffffff; padding: 6px 12px; }"
                "QTabBar::tab:selected { background-color: #4a4a4a; }"
                "QTabBar::tab:hover { background-color: #4a4a4a; }"
                "QScrollBar:vertical { background-color: #2b2b2b; width: 12px; }"
                "QScrollBar::handle:vertical { background-color: #4a4a4a; min-height: 20px; border-radius: 6px; }"
                "QScrollBar::handle:vertical:hover { background-color: #5a5a5a; }"
                "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                "QScrollBar:horizontal { background-color: #2b2b2b; height: 12px; }"
                "QScrollBar::handle:horizontal { background-color: #4a4a4a; min-width: 20px; border-radius: 6px; }"
                "QScrollBar::handle:horizontal:hover { background-color: #5a5a5a; }"
                "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
                "QFrame[frameShape=\"4\"] { background-color: #3c3c3c; }"
                "QToolTip { background-color: #3c3c3c; color: #ffffff; border: 1px solid #555; }";
            
            qApp->setStyleSheet(darkStyle);
        } else {
            qApp->setStyleSheet("");
        }
    }

    void toggleTheme() {
        m_darkMode = !m_darkMode;
        applyTheme(m_darkMode);
        m_darkModeAction->setChecked(m_darkMode);
        saveSettings();
    }

    void zoomIn() {
        if (m_fontSize < m_maxFontSize) {
            m_fontSize += 1;
            m_font.setPointSize(m_fontSize);
            for (int i = 0; i < m_tabWidget->count(); i++) {
                EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
                if (tab && tab->editor) {
                    tab->editor->setEditorFont(m_font);
                }
            }
            updateZoomLabel();
            saveSettings();
        }
    }

    void zoomOut() {
        if (m_fontSize > m_minFontSize) {
            m_fontSize -= 1;
            m_font.setPointSize(m_fontSize);
            for (int i = 0; i < m_tabWidget->count(); i++) {
                EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
                if (tab && tab->editor) {
                    tab->editor->setEditorFont(m_font);
                }
            }
            updateZoomLabel();
            saveSettings();
        }
    }

    void resetZoom() {
        m_fontSize = 10;
        m_font.setPointSize(m_fontSize);
        for (int i = 0; i < m_tabWidget->count(); i++) {
            EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
            if (tab && tab->editor) {
                tab->editor->setEditorFont(m_font);
            }
        }
        updateZoomLabel();
        saveSettings();
    }

    void updateZoomLabel() {
        int percent = qRound((m_fontSize / 10.0) * 100);
        m_zoomLabel->setText(QString("%1%").arg(percent));
    }

    void newFile() {
        createNewTab();
    }

    void openFile() {
        QStringList fileNames = QFileDialog::getOpenFileNames(
            this, "Open Files", "", "Text Files (*.txt);;All Files (*)"
        );
        if (fileNames.isEmpty()) return;

        EditorTab *current = currentTab();
        bool useCurrentTab = (current && 
                             current->fileName.startsWith("Untitled") && 
                             current->editor->toPlainText().isEmpty());

        for (int i = 0; i < fileNames.size(); i++) {
            const QString &fileName = fileNames[i];
            if (useCurrentTab && i == 0) {
                loadFileIntoEditor(current, fileName);
            } else {
                QFile file(fileName);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QMessageBox::warning(this, "Error", "Cannot open file: " + file.errorString());
                    continue;
                }
                QTextStream in(&file);
                QString content = in.readAll();
                EditorTab *tab = createNewTab(content, fileName);
                tab->editor->document()->setModified(false);
                // Update the tab title immediately
                tab->updateTabTitle();
            }
        }
    }
    
    void loadFileIntoEditor(EditorTab *tab, const QString &filePath) {
        if (!tab) return;
        
        // Check if file is already open in another tab
        for (int i = 0; i < m_tabWidget->count(); i++) {
            EditorTab *existingTab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
            if (existingTab && existingTab != tab && existingTab->filePath == filePath) {
                m_tabWidget->setCurrentIndex(i);
                return;
            }
        }
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Cannot open file: " + file.errorString());
            return;
        }
        
        QTextStream in(&file);
        QString content = in.readAll();
        
        // Set the content and update tab info
        tab->editor->setPlainText(content);
        tab->filePath = filePath;
        tab->fileName = QFileInfo(filePath).fileName();
        tab->editor->document()->setModified(false);
        
        // Update the tab title
        tab->updateTabTitle();
        
        updateWindowTitle();
        updateStatusBar();
        updateWordCount();
    }

    void saveCurrentFile() {
        saveFile(currentTab());
    }

    void saveCurrentFileAs() {
        saveFileAs(currentTab());
    }

    bool saveFile(EditorTab *tab) {
        if (!tab) return false;
        if (tab->filePath.isEmpty()) {
            return saveFileAs(tab);
        }
        return saveFileTo(tab, tab->filePath);
    }

    bool saveFileAs(EditorTab *tab) {
        if (!tab) return false;
        QString fileName = QFileDialog::getSaveFileName(
            this, "Save File", "", "Text Files (*.txt);;All Files (*)"
        );
        if (fileName.isEmpty()) return false;
        return saveFileTo(tab, fileName);
    }

    bool saveFileTo(EditorTab *tab, const QString &fileName) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Cannot save file: " + file.errorString());
            return false;
        }

        QTextStream out(&file);
        out << tab->editor->toPlainText();
        tab->filePath = fileName;
        tab->fileName = QFileInfo(fileName).fileName();
        tab->editor->document()->setModified(false);
        tab->updateTabTitle();
        updateWindowTitle();
        return true;
    }

    void saveAllFiles() {
        for (int i = 0; i < m_tabWidget->count(); i++) {
            EditorTab *tab = dynamic_cast<EditorTab*>(m_tabWidget->widget(i));
            if (tab && tab->editor->document()->isModified()) {
                if (tab->filePath.isEmpty()) {
                    QString fileName = QFileDialog::getSaveFileName(
                        this, "Save File", tab->fileName, "Text Files (*.txt);;All Files (*)"
                    );
                    if (fileName.isEmpty()) continue;
                    saveFileTo(tab, fileName);
                } else {
                    saveFileTo(tab, tab->filePath);
                }
            }
        }
    }

    void updateWindowTitle() {
        EditorTab *tab = currentTab();
        if (tab) {
            QString title = tab->fileName.isEmpty() ? "Untitled" : tab->fileName;
            if (tab->editor->document()->isModified()) {
                title += " *";
            }
            setWindowTitle(title + " - Text Editor");
            m_fileLabel->setText(tab->fileName.isEmpty() ? "Untitled" : tab->fileName);
        } else {
            setWindowTitle("Text Editor");
        }
    }

    void updateStatusBar() {
        CodeEditor *editor = currentEditor();
        if (!editor) return;
        
        QTextCursor cursor = editor->textCursor();
        int block = cursor.blockNumber() + 1;
        int col = cursor.columnNumber() + 1;
        m_cursorLabel->setText(QString("Ln %1, Col %2").arg(block).arg(col));

        int selLen = cursor.selectedText().length();
        m_selLabel->setText(selLen > 0 ? QString("(%1 selected)").arg(selLen) : "");
    }

    void updateWordCount() {
        CodeEditor *editor = currentEditor();
        if (!editor) return;
        
        QString text = editor->toPlainText();
        int words = 0;
        if (!text.trimmed().isEmpty()) {
            words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
        }
        m_wordLabel->setText(QString("%1 word%2").arg(words).arg(words != 1 ? "s" : ""));
    }

    QTabWidget *m_tabWidget;
    QLabel *m_cursorLabel;
    QLabel *m_selLabel;
    QLabel *m_wordLabel;
    QLabel *m_encodingLabel;
    QLabel *m_lineEndingLabel;
    QLabel *m_modeLabel;
    QLabel *m_zoomLabel;
    QLabel *m_fileLabel;
    QPushButton *m_themeBtn;
    QAction *m_darkModeAction;
    QAction *m_showLineNumbersAction;
    int m_fontSize;
    int m_minFontSize;
    int m_maxFontSize;
    QFont m_font;
    bool m_darkMode;
    bool m_showLineNumbers;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle(new FlatTextEditorStyle("fusion"));

    TextEditorWindow window;
    
    if (argc > 1) {
        window.openFileInCurrentTab(QString::fromLocal8Bit(argv[1]));
        for (int i = 2; i < argc; i++) {
            QString filePath = QString::fromLocal8Bit(argv[i]);
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                QString content = in.readAll();
                EditorTab *tab = window.createNewTab(content, filePath);
                tab->editor->document()->setModified(false);
                // Update the tab title
                tab->updateTabTitle();
            }
        }
    }
    
    window.show();
    return app.exec();
}
