WITH normalized_file_info AS (
    -- Replace all '\' with '/' in the path
    SELECT replace(path, '\', '/') AS path
    FROM file_info
),
     directories(path, dir, pos) AS (
         -- Start recursion with the normalized paths, an empty 'dir', and starting at position 1
         SELECT path, '', 1
         FROM normalized_file_info

         UNION ALL

         -- Process each character of the path:
         -- If the current character is '/', update 'dir' to the substring up to this character.
         SELECT
             path,
             CASE
                 WHEN substr(path, pos, 1) = '/' THEN substr(path, 1, pos - 1)
                 ELSE dir
                 END,
             pos + 1
         FROM directories
         WHERE pos <= length(path)
     )
-- Select the distinct directory values once each path has been fully processed
SELECT DISTINCT dir AS unique_directory
FROM directories
WHERE pos > length(path)  -- Only consider rows after we've scanned the entire path
  AND dir != '';          -- Exclude empty directory strings
