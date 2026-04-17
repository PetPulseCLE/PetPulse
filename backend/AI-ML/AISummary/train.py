from datasets import load_dataset
import json

# 1. Download the dataset from Hugging Face
dataset = load_dataset("karenwky/pet-health-symptoms-dataset")

# 2. Convert the 'train' split to a list of dictionaries
# (Most HF datasets use 'train' as the primary split)
symptoms_data = dataset['train'].to_pandas().to_dict(orient='records')

# 3. Save it locally as symptoms.json for your API to use
with open('symptoms.json', 'w') as f:
    json.dump(symptoms_data, f, indent=4)

print(f"Successfully saved {len(symptoms_data)} clinical references to symptoms.json")