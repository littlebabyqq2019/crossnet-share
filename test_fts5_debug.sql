-- 测试 FTS5 索引内容
-- 将此文件内容复制到 DB Browser for SQLite 或 sqlite3 命令行执行

-- 1. 查看所有索引的文件
SELECT file_id, file_name FROM files;

-- 2. 查看 FTS5 表的内容（前100个字符）
SELECT file_id, file_name, substr(content, 1, 100) as content_preview FROM files_fts;

-- 3. 测试直接匹配 "雁塔"
SELECT COUNT(*) as count FROM files_fts WHERE files_fts MATCH '雁塔';

-- 4. 测试匹配 "雁"
SELECT COUNT(*) as count FROM files_fts WHERE files_fts MATCH '雁';

-- 5. 测试匹配 "*雁*"（前缀匹配）
SELECT COUNT(*) as count FROM files_fts WHERE files_fts MATCH '雁*';

-- 6. 查看哪些文件包含 "雁塔"（使用 LIKE）
SELECT f.file_name 
FROM files f 
JOIN files_fts fts ON f.file_id = fts.file_id 
WHERE fts.content LIKE '%雁塔%';

-- 7. 使用 snippet 函数查看匹配上下文（如果有结果）
SELECT f.file_name, snippet(files_fts, 2, '[', ']', '...', 10) 
FROM files f 
JOIN files_fts fts ON f.file_id = CAST(fts.file_id AS INTEGER)
WHERE files_fts MATCH '雁塔';

-- 8. 测试英文搜索
SELECT COUNT(*) as count FROM files_fts WHERE files_fts MATCH 'cos';

-- 9. 查看包含 "cos" 的文件
SELECT f.file_name 
FROM files f 
JOIN files_fts fts ON f.file_id = fts.file_id 
WHERE fts.content LIKE '%cos%';
