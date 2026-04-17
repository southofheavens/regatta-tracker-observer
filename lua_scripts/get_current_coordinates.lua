local results = {}
for i, key in ipairs(KEYS) do
    local last_val = redis.call('LINDEX', key, -1)
    table.insert(results, last_val)
end
return results