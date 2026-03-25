-- Add privacy_mode flag to streams table
-- This enables temporary stream pausing for privacy from the Live View page
-- without affecting the administrative 'enabled' flag.

-- migrate:up

ALTER TABLE streams ADD COLUMN privacy_mode INTEGER DEFAULT 0;

-- migrate:down

SELECT 1;

