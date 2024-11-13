FROM public.ecr.aws/lambda/python:3.9

COPY app.py requirements.txt ./
RUN python3.9 -m pip install -r requirements.txt -t .
ADD ./preprocess ./preprocess
ADD ./prod.yml ./prod.yml

# Command can be overwritten by providing a different command in the template directly.
CMD ["app.lambda_handler"]
