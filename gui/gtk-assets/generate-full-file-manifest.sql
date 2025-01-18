WITH fs_max AS (
    -- For each file, get the highest new_ver available in file_size
    SELECT
        id,
        MAX(new_ver) AS max_new_ver
    FROM file_size
    GROUP BY id
)
SELECT
    fi.id,
    REPLACE(fi.path, '\', '/') AS path,
    fs.new_ver,
    fs.size         AS compressed_size,
    fv.size         AS decompressed_size,
    fv.hash         AS decompressed_hash
FROM file_info fi
-- Get the file_size record for the highest new_ver per id
         JOIN fs_max fsm
              ON fi.id = fsm.id
         JOIN file_size fs
              ON fs.id = fsm.id
                  AND fs.new_ver = fsm.max_new_ver
-- Join with file_version on matching id and version
         JOIN file_version fv
              ON fs.id = fv.id
                  AND fs.new_ver = fv.version
ORDER BY fs.id;
