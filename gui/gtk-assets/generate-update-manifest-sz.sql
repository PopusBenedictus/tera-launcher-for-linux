WITH latest_game_version AS (
    SELECT MAX(version) AS latest_version
    FROM version_info
),
     fs_max AS (
         SELECT
             id,
             MAX(new_ver) AS max_new_ver
         FROM file_size
         GROUP BY id
     ),
     versioned_files AS (
         SELECT
             fi.id,
             REPLACE(fi.path, '\', '/') AS path,
             fs.new_ver,
             fs.size         AS compressed_size,
             fv.size         AS decompressed_size,
             fv.hash         AS decompressed_hash
         FROM file_info fi
                  JOIN fs_max fsm
                       ON fi.id = fsm.id
                  JOIN file_size fs
                       ON fs.id = fsm.id
                           AND fs.new_ver = fsm.max_new_ver
                  JOIN file_version fv
                       ON fs.id = fv.id
                           AND fs.new_ver = fv.version
                  CROSS JOIN latest_game_version lgv
         WHERE fs.new_ver >= @current_version
           AND fs.new_ver <= lgv.latest_version
     )
SELECT
    SUM(decompressed_size) AS total_decompressed_size
FROM versioned_files;
